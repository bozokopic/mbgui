#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include "mblaze.h"


typedef struct {
    mbgui_get_directories_cb_t cb;
    gpointer user_data;
    mbgui_directory_t *directories;
    GSubprocess *process;
    GDataInputStream *stream;
} get_directories_data_t;

typedef struct {
    GString *directory;
    mbgui_get_directory_total_cb_t cb;
    gpointer user_data;
    gsize total;
    GSubprocess *process;
    GDataInputStream *stream;
} get_directory_total_data_t;

typedef struct {
    GString *directory;
    mbgui_get_directory_unseen_cb_t cb;
    gpointer user_data;
    gsize unseen;
    GSubprocess *process;
    GDataInputStream *stream;
} get_directory_unseen_data_t;

typedef struct {
    GString *directory;
    mbgui_get_messages_cb_t cb;
    gpointer user_data;
    mbgui_message_t *messages;
    gchar *line_buff[6];
    gsize line_buff_count;
    gint mlist_stdout_fd;
    gint mthread_stdout_fd;
    gint mscan_stdout_fd;
    GInputStream *mscan_stdout;
    GDataInputStream *stream;
} get_messages_data_t;

typedef struct {
    GString *path;
    mbgui_get_message_cb_t cb;
    gpointer user_data;
    gchar buff[1024];
    GString *message;
    GSubprocess *process;
} get_message_data_t;


static void free_directories(mbgui_directory_t *directories) {
    if (!directories)
        return;
    if (directories->path)
        g_string_free(directories->path, TRUE);
    if (directories->name)
        g_string_free(directories->name, TRUE);
    free_directories(directories->children);
    free_directories(directories->next);
    g_free(directories);
}


static void free_messages(mbgui_message_t *messages) {
    if (!messages)
        return;
    if (messages->path)
        g_string_free(messages->path, TRUE);
    if (messages->subject)
        g_string_free(messages->subject, TRUE);
    if (messages->sender)
        g_string_free(messages->sender, TRUE);
    if (messages->date)
        g_string_free(messages->date, TRUE);
    free_messages(messages->children);
    free_messages(messages->next);
    g_free(messages);
}


static void free_get_directories_data(get_directories_data_t *data) {
    free_directories(data->directories);
    g_object_unref(data->stream);
    g_object_unref(data->process);
    g_free(data);
}


static void free_get_directory_total_data(get_directory_total_data_t *data) {
    g_string_free(data->directory, TRUE);
    g_object_unref(data->stream);
    g_object_unref(data->process);
    g_free(data);
}


static void free_get_directory_unseen_data(get_directory_unseen_data_t *data) {
    g_string_free(data->directory, TRUE);
    g_object_unref(data->stream);
    g_object_unref(data->process);
    g_free(data);
}


static void free_get_messages_data(get_messages_data_t *data) {
    g_string_free(data->directory, TRUE);
    free_messages(data->messages);
    for (gsize i = 0; i < data->line_buff_count; ++i)
        g_free(data->line_buff[i]);
    if (data->stream)
        g_object_unref(data->stream);
    if (data->mscan_stdout)
        g_object_unref(data->mscan_stdout);
    if (data->mscan_stdout_fd >= 0)
        g_close(data->mscan_stdout_fd, NULL);
    if (data->mthread_stdout_fd >= 0)
        g_close(data->mthread_stdout_fd, NULL);
    if (data->mlist_stdout_fd >= 0)
        g_close(data->mlist_stdout_fd, NULL);
    free(data);
}


static void free_get_message_data(get_message_data_t *data) {
    g_string_free(data->path, TRUE);
    g_string_free(data->message, TRUE);
    g_object_unref(data->process);
    g_free(data);
}


static void reduce_directory(mbgui_directory_t *directory) {
    for (mbgui_directory_t *child = directory->children;
         child && !child->next && !directory->path;
         child = directory->children) {
        g_string_append_printf(directory->name, "/%s", child->name->str);
        directory->path = child->path;
        child->path = NULL;
        directory->children = child->children;
        child->children = NULL;
        free_directories(child);
    }

    for (mbgui_directory_t *child = directory->children; child;
         child = child->next)
        reduce_directory(child);
}


static mbgui_directory_t *reverse_directories(mbgui_directory_t *directories) {
    mbgui_directory_t *reversed = NULL;
    while (directories) {
        mbgui_directory_t *next = directories->next;
        directories->children = reverse_directories(directories->children);
        directories->next = reversed;
        reversed = directories;
        directories = next;
    }
    return reversed;
}


static mbgui_directory_t *add_directory(mbgui_directory_t *directories,
                                        gchar *path, gchar *name) {
    if (*name != '/')
        return directories;

    gsize index = 1;
    while (name[index] && name[index] != '/')
        ++index;

    if (name[index]) {
        GString *subname = g_string_new_len(name + 1, index - 1);

        mbgui_directory_t *directory = directories;
        while (directory && !g_string_equal(directory->name, subname))
            directory = directory->next;

        if (directory) {
            g_string_free(subname, TRUE);

        } else {
            directory = g_malloc(sizeof(mbgui_directory_t));
            directory->path = NULL;
            directory->name = subname;
            directory->children = NULL;
            directory->next = directories;
        }

        directory->children =
            add_directory(directory->children, path, name + index);
        return directory;
    }

    mbgui_directory_t *directory = g_malloc(sizeof(mbgui_directory_t));
    directory->path = g_string_new(path);
    directory->name = g_string_new(name + 1);
    directory->children = NULL;
    directory->next = directories;
    return directory;
}


static gsize get_message_depth(gchar *line) {
    gsize depth = 0;

    if (line[0] == '.' && line[1] == '.') {
        for (gchar *i = line + 2; g_ascii_isdigit(*i); ++i)
            depth = depth * 10 + (*i - '0');

    } else {
        for (gchar *i = line; *i; ++i) {
            if (*i == ' ')
                depth += 1;
        }
    }

    return depth;
}


static mbgui_message_t *reverse_messages(mbgui_message_t *messages) {
    mbgui_message_t *reversed = NULL;
    while (messages) {
        mbgui_message_t *next = messages->next;
        messages->children = reverse_messages(messages->children);
        messages->next = reversed;
        reversed = messages;
        messages = next;
    }
    return reversed;
}


static mbgui_message_t *add_message(mbgui_message_t *messages, gsize depth,
                                    gchar **lines) {
    if (depth && messages) {
        messages->children = add_message(messages->children, depth - 1, lines);
        return messages;
    }

    mbgui_message_t *message = g_malloc(sizeof(mbgui_message_t));
    message->path = g_string_new(lines[0]);
    message->status = lines[1][0];
    message->subject = g_string_new(lines[2]);
    message->sender = g_string_new(lines[3]);
    message->date = g_string_new(lines[4]);
    message->children = NULL;
    message->next = messages;

    return message;
}


static void on_get_directories_read_line(GObject *source_object,
                                         GAsyncResult *result,
                                         gpointer user_data) {
    get_directories_data_t *data = user_data;

    gchar *line =
        g_data_input_stream_read_line_finish(data->stream, result, NULL, NULL);

    if (!line) {
        data->directories = reverse_directories(data->directories);
        for (mbgui_directory_t *directory = data->directories; directory;
             directory = directory->next) {
            g_string_prepend_c(directory->name, '/');
            reduce_directory(directory);
        }
        data->cb(data->directories, data->user_data);
        free_get_directories_data(data);
        return;
    }

    data->directories = add_directory(data->directories, line, line);
    g_free(line);

    g_data_input_stream_read_line_async(data->stream, 0, NULL,
                                        on_get_directories_read_line, data);
}


static void on_get_directory_total_read_line(GObject *source_object,
                                             GAsyncResult *result,
                                             gpointer user_data) {
    get_directory_total_data_t *data = user_data;

    gchar *line =
        g_data_input_stream_read_line_finish(data->stream, result, NULL, NULL);

    if (!line) {
        data->cb(data->directory->str, data->total, data->user_data);
        free_get_directory_total_data(data);
        return;
    }

    data->total += 1;
    g_free(line);

    g_data_input_stream_read_line_async(data->stream, 0, NULL,
                                        on_get_directory_total_read_line, data);
}


static void on_get_directory_unseen_read_line(GObject *source_object,
                                              GAsyncResult *result,
                                              gpointer user_data) {
    get_directory_unseen_data_t *data = user_data;

    gchar *line =
        g_data_input_stream_read_line_finish(data->stream, result, NULL, NULL);

    if (!line) {
        data->cb(data->directory->str, data->unseen, data->user_data);
        free_get_directory_unseen_data(data);
        return;
    }

    data->unseen += 1;
    g_free(line);

    g_data_input_stream_read_line_async(
        data->stream, 0, NULL, on_get_directory_unseen_read_line, data);
}


static void on_get_messages_read_line(GObject *source_object,
                                      GAsyncResult *result,
                                      gpointer user_data) {
    get_messages_data_t *data = user_data;

    gchar *line =
        g_data_input_stream_read_line_finish(data->stream, result, NULL, NULL);

    if (!line) {
        data->messages = reverse_messages(data->messages);
        data->cb(data->directory->str, data->messages, data->user_data);
        free_get_messages_data(data);
        return;
    }

    data->line_buff[data->line_buff_count++] = line;

    if (data->line_buff_count >= 6) {
        gsize depth = get_message_depth(data->line_buff[0]);
        data->messages =
            add_message(data->messages, depth, data->line_buff + 1);

        for (gsize i = 0; i < data->line_buff_count; ++i)
            g_free(data->line_buff[i]);
        data->line_buff_count = 0;
    }

    g_data_input_stream_read_line_async(data->stream, 0, NULL,
                                        on_get_messages_read_line, data);
}


static void on_get_message_read_all(GObject *source_object,
                                    GAsyncResult *result, gpointer user_data) {
    get_message_data_t *data = user_data;
    GInputStream *stream = g_subprocess_get_stdout_pipe(data->process);

    gsize count = 0;
    g_input_stream_read_all_finish(stream, result, &count, NULL);
    g_string_append_len(data->message, data->buff, count);

    if (count < sizeof(data->buff)) {
        data->cb(data->path->str, data->message->str, data->user_data);
        free_get_message_data(data);
        return;
    }

    g_input_stream_read_all_async(stream, data->buff, sizeof(data->buff), 0,
                                  NULL, on_get_message_read_all, data);
}


void mbgui_get_directories(gchar **argv, mbgui_get_directories_cb_t cb,
                           gpointer user_data) {
    GStrvBuilder *new_argv_builder = g_strv_builder_new();
    g_strv_builder_add_many(new_argv_builder, "mdirs", "-a", NULL);
    g_strv_builder_addv(new_argv_builder, (const gchar **)argv);

    gchar **new_argv = g_strv_builder_end(new_argv_builder);
    g_strv_builder_unref(new_argv_builder);

    get_directories_data_t *data = g_malloc(sizeof(get_directories_data_t));
    data->cb = cb;
    data->user_data = user_data;
    data->directories = NULL;
    data->process = g_subprocess_newv((const gchar **)new_argv,
                                      G_SUBPROCESS_FLAGS_STDOUT_PIPE, NULL);
    data->stream =
        g_data_input_stream_new(g_subprocess_get_stdout_pipe(data->process));

    g_data_input_stream_read_line_async(data->stream, 0, NULL,
                                        on_get_directories_read_line, data);

    g_strfreev(new_argv);
}


void mbgui_get_directory_total(gchar *directory,
                               mbgui_get_directory_total_cb_t cb,
                               gpointer user_data) {
    get_directory_total_data_t *data =
        g_malloc(sizeof(get_directory_total_data_t));
    data->directory = g_string_new(directory);
    data->cb = cb;
    data->user_data = user_data;
    data->total = 0;
    data->process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE, NULL,
                                     "mlist", directory, NULL);
    data->stream =
        g_data_input_stream_new(g_subprocess_get_stdout_pipe(data->process));

    g_data_input_stream_read_line_async(data->stream, 0, NULL,
                                        on_get_directory_total_read_line, data);
}


void mbgui_get_directory_unseen(gchar *directory,
                                mbgui_get_directory_unseen_cb_t cb,
                                gpointer user_data) {
    get_directory_unseen_data_t *data =
        g_malloc(sizeof(get_directory_unseen_data_t));
    data->directory = g_string_new(directory);
    data->cb = cb;
    data->user_data = user_data;
    data->unseen = 0;
    data->process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE, NULL,
                                     "mlist", "-s", directory, NULL);
    data->stream =
        g_data_input_stream_new(g_subprocess_get_stdout_pipe(data->process));

    g_data_input_stream_read_line_async(
        data->stream, 0, NULL, on_get_directory_unseen_read_line, data);
}


void mbgui_get_messages(gchar *directory, mbgui_get_messages_cb_t cb,
                        gpointer user_data) {
    get_messages_data_t *data = g_malloc(sizeof(get_messages_data_t));
    data->directory = g_string_new(directory), data->cb = cb;
    data->user_data = user_data;
    data->messages = NULL;
    data->line_buff_count = 0;
    data->mlist_stdout_fd = -1;
    data->mthread_stdout_fd = -1;
    data->mscan_stdout_fd = -1;
    data->mscan_stdout = NULL;
    data->stream = NULL;

    const gchar *mlist_argv[] = {"mlist", directory, NULL};
    if (!g_spawn_async_with_pipes_and_fds(
            NULL, mlist_argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, -1, -1, -1,
            NULL, NULL, 0, NULL, NULL, &(data->mlist_stdout_fd), NULL, NULL)) {
        g_printerr(">> mlist err");
        free_get_messages_data(data);
        return;
    }

    const gchar *mthread_argv[] = {"mthread", "-r", NULL};
    if (!g_spawn_async_with_pipes_and_fds(
            NULL, mthread_argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
            data->mlist_stdout_fd, -1, -1, NULL, NULL, 0, NULL, NULL,
            &(data->mthread_stdout_fd), NULL, NULL)) {
        g_printerr(">> mthread err");
        free_get_messages_data(data);
        return;
    }

    const gchar *mscan_argv[] = {"mscan", "-f", "%i\n%R\n%u\n%s\n%f\n%D", NULL};
    if (!g_spawn_async_with_pipes_and_fds(
            NULL, mscan_argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
            data->mthread_stdout_fd, -1, -1, NULL, NULL, 0, NULL, NULL,
            &(data->mscan_stdout_fd), NULL, NULL)) {
        g_printerr(">> mscan err");
        free_get_messages_data(data);
        return;
    }

    data->mscan_stdout = g_unix_input_stream_new(data->mscan_stdout_fd, FALSE);
    data->stream = g_data_input_stream_new(data->mscan_stdout);

    g_data_input_stream_read_line_async(data->stream, 0, NULL,
                                        on_get_messages_read_line, data);
}


void mbgui_get_message(gchar *path, mbgui_get_message_cb_t cb,
                       gpointer user_data) {
    get_message_data_t *data = g_malloc(sizeof(get_message_data_t));
    data->path = g_string_new(path);
    data->cb = cb;
    data->user_data = user_data;
    data->message = g_string_new("");
    data->process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE, NULL,
                                     "mshow", path, NULL);

    GInputStream *stream = g_subprocess_get_stdout_pipe(data->process);
    g_input_stream_read_all_async(stream, data->buff, sizeof(data->buff), 0,
                                  NULL, on_get_message_read_all, data);
}

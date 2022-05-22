#include <gtk/gtk.h>
#include "mblaze.h"


typedef struct {
    GtkTreeStore *directories_store;
    GtkTreeSelection *directories_selection;
    GtkTreeStore *messages_store;
    GtkTreeSelection *messages_selection;
    GtkTextBuffer *message_buffer;
} app_data_t;

typedef struct {
    grefcount rc;
    GtkTreeStore *store;
    GtkTreeIter iter;
} tree_store_iter_data_t;


static gchar *get_message_status_icon(mbgui_message_status_t status) {
    switch (status) {
    case MBGUI_MSG_STATUS_SEEN:
        return "mail-read";
    case MBGUI_MSG_STATUS_FLAGGED:
        return "starred";
    case MBGUI_MSG_STATUS_UNSEEN:
        return "mail-unread";
    case MBGUI_MSG_STATUS_TRASHED:
        return "user-trash";
    case MBGUI_MSG_STATUS_VIRTUAL:
        return NULL;
    }
    return NULL;
}


static gchar *get_selected_directory(app_data_t *data) {
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(
            data->directories_selection,
            (GtkTreeModel **)&(data->directories_store), &iter))
        return NULL;

    gchar *result;
    gtk_tree_model_get(GTK_TREE_MODEL(data->directories_store), &iter, 0,
                       &result, -1);
    return result;
}


static gchar *get_selected_message(app_data_t *data) {
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(
            data->messages_selection, (GtkTreeModel **)&(data->messages_store),
            &iter))
        return NULL;

    gchar *result;
    gtk_tree_model_get(GTK_TREE_MODEL(data->messages_store), &iter, 0, &result,
                       -1);
    return result;
}


static void on_get_message(gchar *path, gchar *message, gpointer user_data) {
    app_data_t *data = user_data;

    if (!message || !message[0])
        return;

    gchar *selected_message = get_selected_message(data);
    if (!selected_message)
        return;

    int not_selected = g_strcmp0(path, selected_message);
    g_free(selected_message);
    if (not_selected)
        return;

    gtk_text_buffer_set_text(data->message_buffer, message, -1);
}


static void on_messages_selection_changed(GtkTreeSelection *self,
                                          gpointer user_data) {
    app_data_t *data = user_data;

    gtk_text_buffer_set_text(data->message_buffer, "", 0);

    gchar *message = get_selected_message(data);
    if (!message)
        return;

    // TODO chech virtual

    mbgui_get_message(message, on_get_message, data);
    g_free(message);
}


static void add_message(GtkTreeStore *store, mbgui_message_t *message,
                        GtkTreeIter *parent) {
    GtkTreeIter iter;
    gtk_tree_store_append(store, &iter, parent);
    gtk_tree_store_set(store, &iter, 0, message->path->str, 1,
                       get_message_status_icon(message->status), 2,
                       message->subject->str, 3, message->sender->str, 4,
                       message->date->str, -1);

    for (mbgui_message_t *child = message->children; child; child = child->next)
        add_message(store, child, &iter);
}


static void on_get_messages(gchar *directory, mbgui_message_t *messages,
                            gpointer user_data) {
    app_data_t *data = user_data;

    gchar *selected_directory = get_selected_directory(data);
    if (!selected_directory)
        return;

    int not_selected = g_strcmp0(directory, selected_directory);
    g_free(selected_directory);
    if (not_selected)
        return;

    for (mbgui_message_t *message = messages; message; message = message->next)
        add_message(data->messages_store, message, NULL);
}


static void on_directories_selection_changed(GtkTreeSelection *self,
                                             gpointer user_data) {
    app_data_t *data = user_data;

    gtk_tree_store_clear(data->messages_store);

    gchar *directory = get_selected_directory(data);
    if (!directory)
        return;

    mbgui_get_messages(directory, on_get_messages, data);
    g_free(directory);
}


static GtkWidget *create_directories(app_data_t *data) {
    data->directories_store =
        gtk_tree_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                           G_TYPE_STRING, G_TYPE_STRING);

    GtkCellRenderer *icon_renderer = gtk_cell_renderer_pixbuf_new();
    g_object_set(icon_renderer, "mode", GTK_CELL_RENDERER_MODE_INERT, NULL);

    GtkCellRenderer *left_renderer = gtk_cell_renderer_text_new();
    g_object_set(left_renderer, "xalign", 0.0, "xpad", 5, "mode",
                 GTK_CELL_RENDERER_MODE_INERT, NULL);

    GtkCellRenderer *right_renderer = gtk_cell_renderer_text_new();
    g_object_set(right_renderer, "xalign", 1.0, "mode",
                 GTK_CELL_RENDERER_MODE_INERT, NULL);

    GtkWidget *scrolled_window = gtk_scrolled_window_new();

    GtkWidget *directories =
        gtk_tree_view_new_with_model(GTK_TREE_MODEL(data->directories_store));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window),
                                  directories);

    GtkTreeViewColumn *col_directory = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col_directory, "Directory");
    gtk_tree_view_column_set_expand(col_directory, TRUE);
    gtk_tree_view_column_pack_start(col_directory, icon_renderer, FALSE);
    gtk_tree_view_column_set_attributes(col_directory, icon_renderer,
                                        "icon-name", 1, NULL);
    gtk_tree_view_column_pack_start(col_directory, left_renderer, TRUE);
    gtk_tree_view_column_set_attributes(col_directory, left_renderer, "text", 2,
                                        NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(directories), col_directory);

    GtkTreeViewColumn *col_unseen = gtk_tree_view_column_new_with_attributes(
        "Unseen", right_renderer, "text", 3, NULL);
    gtk_tree_view_column_set_sizing(col_unseen, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_append_column(GTK_TREE_VIEW(directories), col_unseen);

    GtkTreeViewColumn *col_total = gtk_tree_view_column_new_with_attributes(
        "Total", right_renderer, "text", 4, NULL);
    gtk_tree_view_column_set_sizing(col_total, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_append_column(GTK_TREE_VIEW(directories), col_total);

    data->directories_selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(directories));
    gtk_tree_selection_set_mode(data->directories_selection,
                                GTK_SELECTION_SINGLE);
    g_signal_connect(data->directories_selection, "changed",
                     G_CALLBACK(on_directories_selection_changed), data);

    return scrolled_window;
}


static GtkWidget *create_messages(app_data_t *data) {
    data->messages_store =
        gtk_tree_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                           G_TYPE_STRING, G_TYPE_STRING);

    GtkCellRenderer *icon_renderer = gtk_cell_renderer_pixbuf_new();
    g_object_set(icon_renderer, "mode", GTK_CELL_RENDERER_MODE_INERT, NULL);

    GtkCellRenderer *left_renderer = gtk_cell_renderer_text_new();
    g_object_set(left_renderer, "xalign", 0.0, "xpad", 5, "mode",
                 GTK_CELL_RENDERER_MODE_INERT, NULL);

    GtkWidget *scrolled_window = gtk_scrolled_window_new();

    GtkWidget *messages =
        gtk_tree_view_new_with_model(GTK_TREE_MODEL(data->messages_store));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window),
                                  messages);

    GtkTreeViewColumn *col_subject = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col_subject, "Subject");
    gtk_tree_view_column_set_expand(col_subject, TRUE);
    gtk_tree_view_column_pack_start(col_subject, icon_renderer, FALSE);
    gtk_tree_view_column_set_attributes(col_subject, icon_renderer, "icon-name",
                                        1, NULL);
    gtk_tree_view_column_pack_start(col_subject, left_renderer, TRUE);
    gtk_tree_view_column_set_attributes(col_subject, left_renderer, "text", 2,
                                        NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(messages), col_subject);

    GtkTreeViewColumn *col_sender = gtk_tree_view_column_new_with_attributes(
        "Sender", left_renderer, "text", 3, NULL);
    gtk_tree_view_column_set_sizing(col_sender, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_append_column(GTK_TREE_VIEW(messages), col_sender);

    GtkTreeViewColumn *col_date = gtk_tree_view_column_new_with_attributes(
        "Date", left_renderer, "text", 4, NULL);
    gtk_tree_view_column_set_sizing(col_date, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_append_column(GTK_TREE_VIEW(messages), col_date);

    data->messages_selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(messages));
    gtk_tree_selection_set_mode(data->messages_selection, GTK_SELECTION_SINGLE);
    g_signal_connect(data->messages_selection, "changed",
                     G_CALLBACK(on_messages_selection_changed), data);

    return scrolled_window;
}


static GtkWidget *create_message(app_data_t *data) {
    data->message_buffer = gtk_text_buffer_new(NULL);

    GtkWidget *scrolled_window = gtk_scrolled_window_new();

    GtkWidget *message = gtk_text_view_new_with_buffer(data->message_buffer);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(message), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(message), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window),
                                  message);

    return scrolled_window;
}


static GtkWidget *create_window(GtkApplication *app, app_data_t *data) {
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "mbgui");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 800);

    GtkWidget *hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(hpaned), 400);
    gtk_window_set_child(GTK_WINDOW(window), hpaned);

    GtkWidget *directories = create_directories(data);
    gtk_paned_set_start_child(GTK_PANED(hpaned), directories);
    gtk_paned_set_resize_start_child(GTK_PANED(hpaned), TRUE);

    GtkWidget *vpaned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_set_position(GTK_PANED(vpaned), 400);
    gtk_paned_set_end_child(GTK_PANED(hpaned), vpaned);
    gtk_paned_set_resize_end_child(GTK_PANED(hpaned), TRUE);

    GtkWidget *messages = create_messages(data);
    gtk_paned_set_start_child(GTK_PANED(vpaned), messages);
    gtk_paned_set_resize_start_child(GTK_PANED(vpaned), TRUE);

    GtkWidget *message = create_message(data);
    gtk_paned_set_end_child(GTK_PANED(vpaned), message);
    gtk_paned_set_resize_end_child(GTK_PANED(vpaned), TRUE);

    return window;
}


static void on_get_directory_unseen(gchar *directory, gsize unseen,
                                    gpointer user_data) {
    tree_store_iter_data_t *data = user_data;

    GString *unseen_str = g_string_sized_new(8);
    g_string_printf(unseen_str, "%lu", unseen);

    gtk_tree_store_set(data->store, &(data->iter), 3, unseen_str->str, -1);

    g_string_free(unseen_str, TRUE);
    if (g_ref_count_dec((grefcount *)data))
        g_free(data);
}


static void on_get_directory_total(gchar *directory, gsize total,
                                   gpointer user_data) {
    tree_store_iter_data_t *data = user_data;

    GString *total_str = g_string_sized_new(8);
    g_string_printf(total_str, "%lu", total);

    gtk_tree_store_set(data->store, &(data->iter), 4, total_str->str, -1);

    g_string_free(total_str, TRUE);
    if (g_ref_count_dec((grefcount *)data))
        g_free(data);
}


static void add_directory(GtkTreeStore *store, mbgui_directory_t *directory,
                          GtkTreeIter *parent) {
    GtkTreeIter iter;
    gtk_tree_store_append(store, &iter, parent);
    gtk_tree_store_set(store, &iter, 0,
                       (directory->path ? directory->path->str : NULL), 1,
                       (directory->path ? "folder-documents" : "folder"), 2,
                       directory->name->str, -1);

    for (mbgui_directory_t *child = directory->children; child;
         child = child->next)
        add_directory(store, child, &iter);

    if (!directory->path)
        return;

    tree_store_iter_data_t *data = g_malloc(sizeof(tree_store_iter_data_t));
    data->store = store;
    data->iter = iter;

    g_ref_count_init((grefcount *)data);
    mbgui_get_directory_unseen(directory->path->str, on_get_directory_unseen,
                               data);

    g_ref_count_inc((grefcount *)data);
    mbgui_get_directory_total(directory->path->str, on_get_directory_total,
                              data);
}


static void on_get_directories(mbgui_directory_t *directories,
                               gpointer user_data) {
    app_data_t *data = user_data;

    for (mbgui_directory_t *directory = directories; directory;
         directory = directory->next)
        add_directory(data->directories_store, directory, NULL);
}


static void on_command_line(GtkApplication *app,
                            GApplicationCommandLine *command_line,
                            gpointer user_data) {
    app_data_t *data = g_malloc(sizeof(app_data_t));
    GtkWidget *window = create_window(app, data);
    gtk_window_present(GTK_WINDOW(window));

    gchar **argv = g_application_command_line_get_arguments(command_line, NULL);
    mbgui_get_directories(argv, on_get_directories, data);
    g_strfreev(argv);
}


int main(int argc, char **argv) {
    GtkApplication *app =
        gtk_application_new(NULL, G_APPLICATION_HANDLES_COMMAND_LINE);
    g_signal_connect(app, "command-line", G_CALLBACK(on_command_line), NULL);

    return g_application_run(G_APPLICATION(app), argc, argv);
}

#ifndef MBGUI_MBLAZE_H
#define MBGUI_MBLAZE_H

#include <glib.h>


typedef enum {
    MBGUI_MSG_STATUS_SEEN = ' ',
    MBGUI_MSG_STATUS_FLAGGED = '*',
    MBGUI_MSG_STATUS_UNSEEN = '.',
    MBGUI_MSG_STATUS_TRASHED = 'x',
    MBGUI_MSG_STATUS_VIRTUAL = 'v'
} mbgui_message_status_t;

typedef struct mbgui_directory_t {
    GString *path;
    GString *name;
    struct mbgui_directory_t *children;
    struct mbgui_directory_t *next;
} mbgui_directory_t;

typedef struct mbgui_message_t {
    GString *path;
    mbgui_message_status_t status;
    GString *subject;
    GString *sender;
    GString *date;
    struct mbgui_message_t *children;
    struct mbgui_message_t *next;
} mbgui_message_t;


typedef void (*mbgui_get_directories_cb_t)(mbgui_directory_t *directories,
                                           gpointer user_data);
typedef void (*mbgui_get_directory_total_cb_t)(gchar *directory, gsize total,
                                               gpointer user_data);
typedef void (*mbgui_get_directory_unseen_cb_t)(gchar *directory, gsize unseen,
                                                gpointer user_data);
typedef void (*mbgui_get_messages_cb_t)(gchar *directory,
                                        mbgui_message_t *messages,
                                        gpointer user_data);
typedef void (*mbgui_get_message_cb_t)(gchar *path, gchar *message,
                                       gpointer user_data);


void mbgui_get_directories(gchar **argv, mbgui_get_directories_cb_t cb,
                           gpointer user_data);
void mbgui_get_directory_total(gchar *directory,
                               mbgui_get_directory_total_cb_t cb,
                               gpointer user_data);
void mbgui_get_directory_unseen(gchar *directory,
                                mbgui_get_directory_unseen_cb_t cb,
                                gpointer user_data);
void mbgui_get_messages(gchar *directory, mbgui_get_messages_cb_t cb,
                        gpointer user_data);
void mbgui_get_message(gchar *path, mbgui_get_message_cb_t cb,
                       gpointer user_data);

#endif

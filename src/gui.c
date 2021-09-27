#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <string.h>

GtkWidget *quick_connect_btn;

void notify(char* title, char* message){
    char* command = malloc(512);
    char* title_fixed = malloc(255);
    char* message_fixed = malloc(255);

    char* placeholder = malloc(255);

    // Init the values
    strcpy(command, "notify ");
    strcpy(title_fixed, title);
    strcpy(message_fixed, message);

    // Create the title
    strcpy(placeholder, "-t '");
    strcat(title_fixed, "' ");
    strcat(placeholder, title_fixed);
    strcat(command, placeholder);
    
    // Create the message
    strcpy(placeholder, "-m '");
    strcat(message_fixed, "'");
    strcat(placeholder, message_fixed);
    strcat(command, placeholder);

    // Run the notify command
    system(command);

    free(command);
    free(title_fixed);
    free(message_fixed);
    free(placeholder);
}

void ftp_connect()
{
    notify("Flintzilla", "clicked");
}

void quick_connect(GtkWidget *grid)
{
    GtkWidget *host_lbl, *password_lbl, *username_lbl, *port_lbl;
    GtkWidget *host_entry, *password_entry, *username_entry, *port_entry;

    host_lbl = gtk_label_new("Host:");
    host_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), host_lbl, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), host_entry, 2, 0, 1, 1);

    username_lbl = gtk_label_new("Username:");
    username_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), username_lbl, 3, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), username_entry, 4, 0, 1, 1);

    password_lbl = gtk_label_new("Password:");
    password_entry = gtk_entry_new();
    gtk_entry_set_visibility(password_entry, FALSE);
    gtk_grid_attach(GTK_GRID(grid), password_lbl, 5, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), password_entry, 6, 0, 1, 1);

    port_lbl = gtk_label_new("Port:");
    port_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), port_lbl, 7, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), port_entry, 8, 0, 1, 1);

    quick_connect_btn = gtk_button_new_with_label("Submit");
    gtk_grid_attach(GTK_GRID(grid), quick_connect_btn, 9, 0, 1, 1);
    g_signal_connect(quick_connect_btn, "clicked", G_CALLBACK(ftp_connect), NULL);
}

int main(int argc, char **argv)
{
    // Starting the gtk
    GtkWidget *window, *grid;

    gtk_init(&argc, &argv);
    // Creating a basic window
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "FlintZilla");
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    grid = gtk_grid_new();
    gtk_container_add(GTK_GRID(window), grid);

    quick_connect(grid);

    // Run and show everything
    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
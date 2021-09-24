#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>

int main(int argc, char **argv){
    // Starting the gtk
    GtkWidget *window, *grid;

    gtk_init(&argc, &argv);
    // Creating a basic window
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    grid = gtk_grid_new();
    gtk_container_add(GTK_GRID(window), grid);

    // Run and show everything
    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
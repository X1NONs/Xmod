#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../core/backend.h"
#include "../include/xmod/uapi.h"

// --- Global UI Widgets ---
static GtkWidget *lbl_status;
static GtkWidget *btn_load, *btn_unload;
static GtkWidget *ent_pid, *ent_addr, *ent_len;
static GtkTextBuffer *txt_hex_buffer;

// --- Helper: Format Hex Dump ---
static void format_hex_dump(uint64_t base_addr, uint8_t *data, size_t len) {
    gtk_text_buffer_set_text(txt_hex_buffer, "", 0);
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(txt_hex_buffer, &iter);

    char line[128];
    for (size_t i = 0; i < len; i += 16) {
        int offset = snprintf(line, sizeof(line), "%016llx  ", (unsigned long long)(base_addr + i));
        for (size_t j = 0; j < 16; j++) {
            if (i + j < len) offset += snprintf(line + offset, sizeof(line) - offset, "%02x ", data[i + j]);
            else offset += snprintf(line + offset, sizeof(line) - offset, "   ");
            if (j == 7) offset += snprintf(line + offset, sizeof(line) - offset, " ");
        }
        offset += snprintf(line + offset, sizeof(line) - offset, " |");
        for (size_t j = 0; j < 16 && i + j < len; j++) {
            char c = data[i + j];
            offset += snprintf(line + offset, sizeof(line) - offset, "%c", (c >= 32 && c <= 126) ? c : '.');
        }
        offset += snprintf(line + offset, sizeof(line) - offset, "|\n");
        gtk_text_buffer_insert(txt_hex_buffer, &iter, line, -1);
    }
}

// --- Driver Management ---
static bool is_driver_loaded() {
    return access("/dev/xmod", F_OK) == 0;
}

static void update_driver_ui() {
    if (is_driver_loaded()) {
        gtk_label_set_text(GTK_LABEL(lbl_status), "Driver Status: LOADED (/dev/xmod active)");
        gtk_widget_set_sensitive(btn_load, FALSE);
        gtk_widget_set_sensitive(btn_unload, TRUE);
        
        // Open backend if not already open
        if (xmod_backend_open() < 0) {
            gtk_label_set_text(GTK_LABEL(lbl_status), "Driver Status: ERROR (Failed to open backend)");
        }
    } else {
        gtk_label_set_text(GTK_LABEL(lbl_status), "Driver Status: UNLOADED");
        gtk_widget_set_sensitive(btn_load, TRUE);
        gtk_widget_set_sensitive(btn_unload, FALSE);
        xmod_backend_close();
    }
}

static void on_load_clicked(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    // modprobe requires the module to be installed via install.sh
    int ret = system("modprobe xmod");
    update_driver_ui();
    
    if (ret != 0) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(btn))),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                                   "Failed to load driver. Did you run install.sh?");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

static void on_unload_clicked(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    system("rmmod xmod");
    update_driver_ui();
}

// --- Memory Read/Write Actions ---
static void on_read_clicked(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    
    const char *pid_str = gtk_entry_get_text(GTK_ENTRY(ent_pid));
    const char *addr_str = gtk_entry_get_text(GTK_ENTRY(ent_addr));
    const char *len_str = gtk_entry_get_text(GTK_ENTRY(ent_len));

    if (!pid_str || !addr_str || !len_str || strlen(pid_str) == 0) {
        gtk_text_buffer_set_text(txt_hex_buffer, "Error: Missing PID, Address, or Length.", -1);
        return;
    }

    pid_t pid = (pid_t)strtoul(pid_str, NULL, 0);
    uint64_t addr = strtoull(addr_str, NULL, 0);
    size_t len = (size_t)strtoull(len_str, NULL, 0);

    if (len > 4096) len = 4096; // Cap UI hex dump at 4KB for performance

    uint32_t handle = 0;
    if (xmod_backend_open_process(pid, false, &handle) < 0) {
        gtk_text_buffer_set_text(txt_hex_buffer, "Error: Failed to attach to process.", -1);
        return;
    }

    uint8_t *buf = malloc(len);
    if (!buf) {
        xmod_backend_close_target(handle);
        return;
    }

    size_t done = 0;
    xmod_backend_read(handle, addr, buf, len, &done);
    
    format_hex_dump(addr, buf, done);
    
    free(buf);
    xmod_backend_close_target(handle);
}

// --- UI Setup ---
static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Xmod - Linux Memory Framework");
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 700);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), main_box);

    // --- Top Bar: Driver Control ---
    GtkWidget *top_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(top_bar, 10);
    gtk_widget_set_margin_start(top_bar, 10);
    gtk_widget_set_margin_end(top_bar, 10);
    gtk_box_pack_start(GTK_BOX(main_box), top_bar, FALSE, FALSE, 0);

    lbl_status = gtk_label_new("Driver Status: CHECKING...");
    gtk_widget_set_halign(lbl_status, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(top_bar), lbl_status, TRUE, TRUE, 0);

    btn_load = gtk_button_new_with_label("Load Driver");
    g_signal_connect(btn_load, "clicked", G_CALLBACK(on_load_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(top_bar), btn_load, FALSE, FALSE, 0);

    btn_unload = gtk_button_new_with_label("Unload Driver");
    g_signal_connect(btn_unload, "clicked", G_CALLBACK(on_unload_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(top_bar), btn_unload, FALSE, FALSE, 0);

    // --- Notebook (Tabs) ---
    GtkWidget *notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(main_box), notebook, TRUE, TRUE, 0);

    // Tab 1: Process Memory
    GtkWidget *page_proc = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_start(page_proc, 10);
    gtk_widget_set_margin_end(page_proc, 10);
    
    GtkWidget *controls_proc = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(page_proc), controls_proc, FALSE, FALSE, 5);

    gtk_box_pack_start(GTK_BOX(controls_proc), gtk_label_new("PID:"), FALSE, FALSE, 5);
    ent_pid = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ent_pid), "1234");
    gtk_box_pack_start(GTK_BOX(controls_proc), ent_pid, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(controls_proc), gtk_label_new("Addr:"), FALSE, FALSE, 5);
    ent_addr = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ent_addr), "0x7f0000000000");
    gtk_box_pack_start(GTK_BOX(controls_proc), ent_addr, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(controls_proc), gtk_label_new("Len:"), FALSE, FALSE, 5);
    ent_len = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(ent_len), "256");
    gtk_box_pack_start(GTK_BOX(controls_proc), ent_len, FALSE, FALSE, 0);

    GtkWidget *btn_read = gtk_button_new_with_label("Read Memory");
    g_signal_connect(btn_read, "clicked", G_CALLBACK(on_read_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(controls_proc), btn_read, FALSE, FALSE, 5);

    // Hex Viewer
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    
    GtkWidget *text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(text_view), TRUE);
    txt_hex_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    
    gtk_container_add(GTK_CONTAINER(scroll), text_view);
    gtk_box_pack_start(GTK_BOX(page_proc), scroll, TRUE, TRUE, 5);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page_proc, gtk_label_new("Process Memory"));

    // Tab 2: Physical Memory (Placeholder for now, uses same logic)
    GtkWidget *page_phys = gtk_label_new("Physical Memory UI coming in next commit...");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page_phys, gtk_label_new("Physical Memory"));

    // Initial state
    update_driver_ui();
    gtk_widget_show_all(window);
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.xmod.gui", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    
    xmod_backend_close();
    return status;
}

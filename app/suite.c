#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <dialogs/dialogs.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <stdio.h>

int32_t suite_rx_monitor(void* channel);
int32_t suite_capture(void* channel);
int32_t suite_tx_audio(void* path);
bool suite_tx_command(uint8_t command);
bool suite_play_capture(void);

enum {PAGE_HOME, PAGE_RX, PAGE_TX, PAGE_TARGET};
enum {
    ITEM_RX = 1, ITEM_TX, ITEM_ABOUT,
    ITEM_MONITOR = 10, ITEM_RECORD, ITEM_LISTEN, ITEM_CHANNEL, ITEM_BACK,
    ITEM_CH1 = 20, ITEM_CH18, ITEM_CH19, ITEM_ALLOFF, ITEM_AUDIO,
    ITEM_CHOOSE,
    ITEM_TARGET_BASE = 100,
    ITEM_EXIT = 255,
};

typedef struct {
    ViewDispatcher* dispatcher;
    uint32_t choice;
} MenuContext;

static void selected(void* context, uint32_t index) {
    MenuContext* menu = context;
    menu->choice = index;
    view_dispatcher_stop(menu->dispatcher);
}

static bool back(void* context) {
    MenuContext* menu = context;
    menu->choice = ITEM_BACK;
    view_dispatcher_stop(menu->dispatcher);
    return true;
}

static void message(DialogsApp* dialogs, const char* title, const char* body) {
    DialogMessage* dialog = dialog_message_alloc();
    dialog_message_set_header(dialog, title, 64, 11, AlignCenter, AlignTop);
    dialog_message_set_text(dialog, body, 64, 34, AlignCenter, AlignCenter);
    dialog_message_set_buttons(dialog, NULL, "OK", NULL);
    dialog_message_show(dialogs, dialog);
    dialog_message_free(dialog);
}

static bool confirm_tx(DialogsApp* dialogs, const char* title) {
    DialogMessage* dialog = dialog_message_alloc();
    dialog_message_set_header(dialog, title, 64, 10, AlignCenter, AlignTop);
    dialog_message_set_text(dialog, "T130U off; your T131U on", 64, 32, AlignCenter, AlignCenter);
    dialog_message_set_buttons(dialog, "Cancel", NULL, "Send");
    DialogMessageButton answer = dialog_message_show(dialogs, dialog);
    dialog_message_free(dialog);
    return answer == DialogMessageButtonRight;
}

static uint32_t run_menu(Gui* gui, uint8_t page, uint8_t channel) {
    MenuContext state = {.choice = ITEM_EXIT};
    char labels[19][24];
    ViewDispatcher* dispatcher = view_dispatcher_alloc();
    state.dispatcher = dispatcher;
    Submenu* submenu = submenu_alloc();
    view_dispatcher_set_event_callback_context(dispatcher, &state);
    view_dispatcher_set_navigation_event_callback(dispatcher, back);
    if(page == PAGE_HOME) {
        submenu_set_header(submenu, "Retekess Suite");
        submenu_add_item(submenu, "Receive", ITEM_RX, selected, &state);
        submenu_add_item(submenu, "Transmitter", ITEM_TX, selected, &state);
        submenu_add_item(submenu, "About / limits", ITEM_ABOUT, selected, &state);
    } else if(page == PAGE_RX) {
        char header[32];
        if(channel == 20) snprintf(header, sizeof(header), "Receiver  CHS");
        else snprintf(header, sizeof(header), "Receiver  CH%u", channel);
        submenu_set_header(submenu, header);
        submenu_add_item(submenu, "Monitor + events", ITEM_MONITOR, selected, &state);
        submenu_add_item(submenu, "Capture 1.6 s", ITEM_RECORD, selected, &state);
        submenu_add_item(submenu, "Listen last (test)", ITEM_LISTEN, selected, &state);
        submenu_add_item(submenu, "Change channel", ITEM_CHANNEL, selected, &state);
        submenu_add_item(submenu, "Back", ITEM_BACK, selected, &state);
    } else if(page == PAGE_TX) {
        submenu_set_header(submenu, "Transmitter / controls");
        submenu_add_item(submenu, "Set channel 1-19", ITEM_CHOOSE, selected, &state);
        submenu_add_item(submenu, "All Off", ITEM_ALLOFF, selected, &state);
        submenu_add_item(submenu, "Encoded audio .bin", ITEM_AUDIO, selected, &state);
        submenu_add_item(submenu, "Back", ITEM_BACK, selected, &state);
    } else {
        submenu_set_header(submenu, "Select target channel");
        for(uint8_t ch = 1; ch <= 19; ch++) {
            snprintf(labels[ch - 1], sizeof(labels[ch - 1]),
                "CH%02u  %s", ch, (ch == 1 || ch == 18 || ch == 19) ? "known" : "test");
            submenu_add_item(submenu, labels[ch - 1], ITEM_TARGET_BASE + ch, selected, &state);
        }
        submenu_add_item(submenu, "Back", ITEM_BACK, selected, &state);
    }
    view_dispatcher_add_view(dispatcher, 0, submenu_get_view(submenu));
    view_dispatcher_attach_to_gui(dispatcher, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(dispatcher, 0);
    view_dispatcher_run(dispatcher);
    view_dispatcher_remove_view(dispatcher, 0);
    submenu_free(submenu);
    view_dispatcher_free(dispatcher);
    return state.choice;
}

int32_t retekess_suite_app(void* p) {
    UNUSED(p);
    Gui* gui = furi_record_open(RECORD_GUI);
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notification, &sequence_display_backlight_enforce_on);
    uint8_t page = PAGE_HOME, channel = 19;
    bool running = true;
    while(running) {
        uint32_t choice = run_menu(gui, page, channel);
        if(choice == ITEM_EXIT || choice == ITEM_BACK) {
            if(page == PAGE_HOME) running = false;
            else if(page == PAGE_TARGET) page = PAGE_TX;
            else page = PAGE_HOME;
        } else if(choice == ITEM_RX) page = PAGE_RX;
        else if(choice == ITEM_TX) page = PAGE_TX;
        else if(choice == ITEM_CHOOSE) page = PAGE_TARGET;
        else if(choice == ITEM_ABOUT) {
            message(dialogs, "Retekess Suite 0.1", "Tested: CH1/18/19, All Off\nCH1/17/18/19/CHS RX");
        } else if(choice == ITEM_MONITOR) {
            suite_rx_monitor((void*)(uintptr_t)channel);
        } else if(choice == ITEM_RECORD) {
            int32_t ok = suite_capture((void*)(uintptr_t)channel);
            message(dialogs, "Audio capture", ok == 0 ?
                "Raw + A-law saved\nin retekess_probe" : "Capture failed; see CSV");
        } else if(choice == ITEM_LISTEN) {
            bool ok = suite_play_capture();
            message(dialogs, "Speaker test", ok ?
                "Played saved A-law.\nSound is experimental." : "No saved audio or speaker busy");
        } else if(choice == ITEM_CHANNEL) {
            channel = channel == 1 ? 17 : channel == 17 ? 18 :
                channel == 18 ? 19 : channel == 19 ? 20 : 1;
        } else if(choice > ITEM_TARGET_BASE && choice <= ITEM_TARGET_BASE + 19) {
            uint8_t target = choice - ITEM_TARGET_BASE;
            char title[24];
            snprintf(title, sizeof(title), "Set CH%u", target);
            if(confirm_tx(dialogs, title)) {
                bool ok = suite_tx_command(4 + target);
                message(dialogs, title, ok ?
                    ((target == 1 || target == 18 || target == 19) ?
                    "Sent; receiver check needed" : "Experimental target sent") : "Radio transmission failed");
            }
        } else if(choice >= ITEM_CH1 && choice <= ITEM_ALLOFF) {
            const char* title = choice == ITEM_ALLOFF ? "All Off" :
                choice == ITEM_CH1 ? "Set CH1" : choice == ITEM_CH18 ? "Set CH18" : "Set CH19";
            if(confirm_tx(dialogs, title)) {
                uint8_t cmd = choice == ITEM_ALLOFF ? 1 :
                    choice == ITEM_CH1 ? 4 : choice == ITEM_CH18 ? 2 : 3;
                bool ok = suite_tx_command(cmd);
                message(dialogs, title, ok ? "Radio transmission finished" : "Radio transmission failed");
            }
        } else if(choice == ITEM_AUDIO) {
            FuriString* path = furi_string_alloc_set("/ext/retekess_probe");
            FuriString* result = furi_string_alloc();
            DialogsFileBrowserOptions options;
            dialog_file_browser_set_basic_options(&options, ".bin", NULL);
            options.base_path = "/ext/retekess_probe";
            if(dialog_file_browser_show(dialogs, result, path, &options)) {
                if(confirm_tx(dialogs, "Send audio file")) {
                    int32_t ok = suite_tx_audio((void*)furi_string_get_cstr(result));
                    message(dialogs, "Audio TX", ok == 0 ? "Transmission finished" : "File or TX error");
                }
            }
            furi_string_free(result);
            furi_string_free(path);
        }
    }
    notification_message(notification, &sequence_display_backlight_enforce_auto);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_GUI);
    return 0;
}

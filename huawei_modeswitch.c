/*
 * Huawei Mode Switch (Universal)
 * Switches Huawei modems from ZeroCD/Storage mode to Modem mode
 * Supports multiple PIDs: 0x1446, 0x14FE, and others
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>

#define HUAWEI_VENDOR_ID    0x12D1

// ZeroCD/Storage mode PIDs that need switching
static uint16_t zerocd_pids[] = {
    // === Classic 3G ZeroCD ===
    0x1446,  // E1550/E1756/E173 ZeroCD
    0x14fe,  // E303/E3131/E1550 Intermediate
    0x1520,  // K3765 ZeroCD
    0x1505,  // E3131/E398 ZeroCD
    0x14D1,  // E173 ZeroCD
    0x1c0b,  // E3531/E173s ZeroCD
    
    // === LTE ZeroCD ===
    0x1f01,  // E3131/E353/E3372/E8372 ZeroCD (common)
    0x1da1,  // E3372 ZeroCD
    0x1f1e,  // K5160 ZeroCD
    0x15ca,  // E3131h-2 ZeroCD
    
    // === Other ZeroCD modes ===
    0x1521,  // K4505 ZeroCD
    0x1575,  // K5150 ZeroCD
    0x157c,  // E3276 ZeroCD
    0x157d,  // E3276 ZeroCD alternate
    0x1582,  // E8278 ZeroCD
    0x1583,  // E8278 ZeroCD alternate
    0x1588,  // E3372 variant ZeroCD
    0x15b6,  // E3331 ZeroCD
    0x1c1b,  // E3531 (sometimes ZeroCD)
    0
};

// Target modem mode PIDs (for verification after switch)
static uint16_t modem_pids[] = {
    // === Classic 3G/HSPA Modem ===
    0x1001,  // E169/E620/E800/E1550 HSDPA Modem
    0x1003,  // E1550 Modem
    0x140c,  // E180/E1550 Modem
    0x1406,  // E1750 Modem
    0x1436,  // E173/E1750 Modem
    0x1465,  // K3765 Modem
    0x14AC,  // E1820 Modem
    0x14C6,  // K4605 Modem
    0x14C9,  // K4505 Modem
    0x1c05,  // E173 Modem
    0x1c07,  // E173s Modem
    
    // === E3xx Series Modem ===
    0x1506,  // E303/E3131/MS2372 Modem
    0x14db,  // E3131/E353 HiLink/NCM
    
    // === LTE Modem ===
    0x1442,  // E3372 Stick mode
    0x14dc,  // E3372/E8372 HiLink
    0x155e,  // E8372 NCM/Stick mode
    0x157f,  // E8372 alternate
    0x1592,  // E8372h mode
    0x15c1,  // ME906s LTE M.2
    0x1573,  // K5150 Modem
    0x1576,  // K5160 Modem
    
    // === Legacy ===
    0x1404,  // E1752 Modem
    0x1411,  // E510 Modem
    0x141b,  // E1752 alternate
    0x1464,  // K4510/K4511 Modem
    0x14ba,  // E173 alternate
    0
};

// Standard SCSI commands wrapped in USB Mass Storage CBW
static unsigned char huawei_switch_msg[] = {
    0x55, 0x53, 0x42, 0x43,  // "USBC" signature
    0x12, 0x34, 0x56, 0x78,  // Tag
    0x00, 0x00, 0x00, 0x00,  // Data transfer length
    0x00,                     // Flags (OUT)
    0x00,                     // LUN
    0x11,                     // Command length
    // Huawei specific SCSI command
    0x11, 0x06, 0x20, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Alternative Huawei command
static unsigned char huawei_switch_msg2[] = {
    0x55, 0x53, 0x42, 0x43,  // "USBC"
    0x12, 0x34, 0x56, 0x79,  // Tag
    0x00, 0x00, 0x00, 0x00,  // Transfer length
    0x00, 0x00, 0x11,        // Flags, LUN, CDB length
    0x11, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Standard "Eject Media" SCSI command
static unsigned char eject_msg[] = {
    0x55, 0x53, 0x42, 0x43,  // "USBC"
    0x12, 0x34, 0x56, 0x7A,  // Tag
    0x00, 0x00, 0x00, 0x00,  // Transfer length
    0x00, 0x00, 0x06,        // Flags, LUN, CDB length
    0x1b, 0x00, 0x00, 0x00, 0x02, 0x00,  // START STOP UNIT with eject
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const char* get_pid_name(uint16_t pid) {
    switch(pid) {
        // ZeroCD modes
        case 0x1446: return "E1550/E1756/E173 ZeroCD";
        case 0x14FE: return "E303/E3131 Intermediate";
        case 0x1f01: return "E3131/E3372/E8372 ZeroCD";
        case 0x1520: return "K3765 ZeroCD";
        case 0x1505: return "E3131/E398 ZeroCD";
        case 0x14D1: return "E173 ZeroCD";
        case 0x1c0b: return "E3531 ZeroCD";
        case 0x1da1: return "E3372 ZeroCD";
        case 0x1f1e: return "K5160 ZeroCD";
        case 0x15ca: return "E3131h-2 ZeroCD";
        case 0x1521: return "K4505 ZeroCD";
        case 0x1575: return "K5150 ZeroCD";
        case 0x157c: return "E3276 ZeroCD";
        case 0x157d: return "E3276 ZeroCD Alt";
        case 0x1582: return "E8278 ZeroCD";
        case 0x1583: return "E8278 ZeroCD Alt";
        case 0x1588: return "E3372 Variant ZeroCD";
        case 0x15b6: return "E3331 ZeroCD";
        // Modem modes
        case 0x1001: return "E169/E620/E800/E1550 Modem";
        case 0x1003: return "E1550 Modem";
        case 0x140c: return "E180/E1550 Modem";
        case 0x1406: return "E1750 Modem";
        case 0x1436: return "E173/E1750 Modem";
        case 0x1465: return "K3765 Modem";
        case 0x14AC: return "E1820 Modem";
        case 0x14C6: return "K4605 Modem";
        case 0x14C9: return "K4505 Modem";
        case 0x1c05: return "E173 Modem";
        case 0x1c07: return "E173s Modem";
        case 0x1c1b: return "E3531 Modem";
        case 0x1506: return "E303/E3131/MS2372 Modem";
        case 0x14db: return "E3131/E353 HiLink";
        case 0x1442: return "E3372 Stick";
        case 0x14dc: return "E3372/E8372 HiLink";
        case 0x155e: return "E8372 NCM";
        case 0x157f: return "E8372 Alt";
        case 0x1592: return "E8372h";
        case 0x15c1: return "ME906s LTE";
        case 0x1573: return "K5150 Modem";
        case 0x1576: return "K5160 Modem";
        case 0x1404: return "E1752 Modem";
        case 0x1411: return "E510 Modem";
        case 0x141b: return "E1752 Alt";
        case 0x1464: return "K4510/K4511 Modem";
        case 0x14ba: return "E173 Alt";
        default: return "Unknown Huawei";
    }
}

int is_zerocd_pid(uint16_t pid) {
    for (int i = 0; zerocd_pids[i] != 0; i++) {
        if (zerocd_pids[i] == pid) return 1;
    }
    return 0;
}

int is_modem_pid(uint16_t pid) {
    for (int i = 0; modem_pids[i] != 0; i++) {
        if (modem_pids[i] == pid) return 1;
    }
    return 0;
}

void print_hex(const char* label, unsigned char* data, int len) {
    printf("%s: ", label);
    for (int i = 0; i < len && i < 31; i++) {
        printf("%02x ", data[i]);
    }
    printf("\n");
}

int find_bulk_out_endpoint(libusb_device *dev, int *out_interface) {
    struct libusb_config_descriptor *config;
    int r = libusb_get_active_config_descriptor(dev, &config);
    if (r < 0) return -1;
    
    int ep_out = -1;
    *out_interface = 0;
    
    // First look for Mass Storage interface (class 0x08)
    for (int i = 0; i < config->bNumInterfaces && ep_out < 0; i++) {
        const struct libusb_interface *iface = &config->interface[i];
        for (int j = 0; j < iface->num_altsetting && ep_out < 0; j++) {
            const struct libusb_interface_descriptor *setting = &iface->altsetting[j];
            
            if (setting->bInterfaceClass == 0x08) {  // Mass Storage
                for (int k = 0; k < setting->bNumEndpoints; k++) {
                    const struct libusb_endpoint_descriptor *ep = &setting->endpoint[k];
                    if ((ep->bmAttributes & 0x03) == LIBUSB_TRANSFER_TYPE_BULK) {
                        if (!(ep->bEndpointAddress & 0x80)) {
                            ep_out = ep->bEndpointAddress;
                            *out_interface = setting->bInterfaceNumber;
                            break;
                        }
                    }
                }
            }
        }
    }
    
    // Fallback: any bulk OUT endpoint
    if (ep_out < 0) {
        for (int i = 0; i < config->bNumInterfaces && ep_out < 0; i++) {
            const struct libusb_interface *iface = &config->interface[i];
            for (int j = 0; j < iface->num_altsetting && ep_out < 0; j++) {
                const struct libusb_interface_descriptor *setting = &iface->altsetting[j];
                for (int k = 0; k < setting->bNumEndpoints; k++) {
                    const struct libusb_endpoint_descriptor *ep = &setting->endpoint[k];
                    if ((ep->bmAttributes & 0x03) == LIBUSB_TRANSFER_TYPE_BULK) {
                        if (!(ep->bEndpointAddress & 0x80)) {
                            ep_out = ep->bEndpointAddress;
                            *out_interface = setting->bInterfaceNumber;
                            break;
                        }
                    }
                }
            }
        }
    }
    
    libusb_free_config_descriptor(config);
    return ep_out;
}

int try_bulk_transfer(libusb_device_handle *handle, int ep_out, unsigned char* msg, int msg_len, const char* desc) {
    int transferred;
    int r;
    
    printf("\n[%s]\n", desc);
    print_hex("Sending", msg, msg_len);
    
    r = libusb_bulk_transfer(handle, ep_out, msg, msg_len, &transferred, 2000);
    if (r == 0) {
        printf("Success! Sent %d bytes\n", transferred);
        return 0;
    } else {
        printf("Failed: %s\n", libusb_strerror(r));
        return -1;
    }
}

int try_control_transfer(libusb_device_handle *handle) {
    int r;
    
    printf("\n[Trying USB Control Transfers]\n");
    
    // Method 1: Huawei specific control message
    printf("Method 1: Huawei control message...\n");
    r = libusb_control_transfer(handle,
        LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_OUT,
        LIBUSB_REQUEST_SET_FEATURE,
        0x0001,
        0x0000,
        NULL, 0,
        1000);
    printf("  Result: %s\n", r < 0 ? libusb_strerror(r) : "OK");
    
    // Method 2: Set configuration
    printf("Method 2: Set configuration...\n");
    r = libusb_set_configuration(handle, 1);
    printf("  Result: %s\n", r < 0 ? libusb_strerror(r) : "OK");
    
    // Method 3: Device reset
    printf("Method 3: USB device reset...\n");
    r = libusb_reset_device(handle);
    if (r == LIBUSB_ERROR_NOT_FOUND) {
        printf("  Device disconnected (mode switch may have worked!)\n");
        return 1;
    }
    printf("  Result: %s\n", r < 0 ? libusb_strerror(r) : "OK");
    
    return 0;
}

void scan_huawei_devices(libusb_context *ctx, int *found_zerocd, int *found_modem) {
    libusb_device **devs;
    ssize_t cnt = libusb_get_device_list(ctx, &devs);
    
    *found_zerocd = 0;
    *found_modem = 0;
    
    printf("\nHuawei devices found:\n");
    int found = 0;
    
    for (ssize_t i = 0; i < cnt; i++) {
        struct libusb_device_descriptor desc;
        libusb_get_device_descriptor(devs[i], &desc);
        if (desc.idVendor == HUAWEI_VENDOR_ID) {
            const char* mode = "";
            if (is_zerocd_pid(desc.idProduct)) {
                mode = " [ZeroCD/Storage]";
                (*found_zerocd)++;
            } else if (is_modem_pid(desc.idProduct)) {
                mode = " [Modem Mode]";
                (*found_modem)++;
            }
            printf("  12d1:%04x - %s%s\n", desc.idProduct, get_pid_name(desc.idProduct), mode);
            found++;
        }
    }
    
    if (!found) {
        printf("  No Huawei devices found\n");
    }
    
    libusb_free_device_list(devs, 1);
}

libusb_device_handle* find_zerocd_device(libusb_context *ctx, uint16_t *found_pid) {
    for (int i = 0; zerocd_pids[i] != 0; i++) {
        libusb_device_handle *h = libusb_open_device_with_vid_pid(ctx, HUAWEI_VENDOR_ID, zerocd_pids[i]);
        if (h) {
            *found_pid = zerocd_pids[i];
            return h;
        }
    }
    return NULL;
}

int switch_device(libusb_context *ctx, libusb_device_handle *handle, uint16_t pid) {
    libusb_device *dev = libusb_get_device(handle);
    int interface_num = 0;
    int r;
    
    printf("Switching device 12d1:%04x (%s)...\n\n", pid, get_pid_name(pid));
    
    // Get device info
    struct libusb_device_descriptor desc;
    libusb_get_device_descriptor(dev, &desc);
    printf("bNumConfigurations: %d\n", desc.bNumConfigurations);
    
    struct libusb_config_descriptor *config;
    libusb_get_active_config_descriptor(dev, &config);
    printf("bNumInterfaces: %d\n\n", config->bNumInterfaces);
    
    // Print interface info
    for (int i = 0; i < config->bNumInterfaces; i++) {
        const struct libusb_interface *iface = &config->interface[i];
        for (int j = 0; j < iface->num_altsetting; j++) {
            const struct libusb_interface_descriptor *setting = &iface->altsetting[j];
            printf("Interface %d: class=0x%02x subclass=0x%02x protocol=0x%02x endpoints=%d\n",
                   setting->bInterfaceNumber,
                   setting->bInterfaceClass,
                   setting->bInterfaceSubClass,
                   setting->bInterfaceProtocol,
                   setting->bNumEndpoints);
            
            for (int k = 0; k < setting->bNumEndpoints; k++) {
                const struct libusb_endpoint_descriptor *ep = &setting->endpoint[k];
                printf("  Endpoint 0x%02x: type=%d\n", 
                       ep->bEndpointAddress,
                       ep->bmAttributes & 0x03);
            }
        }
    }
    
    libusb_free_config_descriptor(config);
    
    // Find bulk OUT endpoint
    int ep_out = find_bulk_out_endpoint(dev, &interface_num);
    if (ep_out >= 0) {
        printf("\nFound bulk OUT endpoint: 0x%02x on interface %d\n", ep_out, interface_num);
    }
    
    // Detach kernel drivers
    printf("\n[Detaching kernel drivers]\n");
    for (int i = 0; i < 8; i++) {
        r = libusb_kernel_driver_active(handle, i);
        if (r == 1) {
            printf("Detaching driver from interface %d...\n", i);
            libusb_detach_kernel_driver(handle, i);
        }
    }
    
    // Claim interface
    printf("\n[Claiming interface %d]\n", interface_num);
    r = libusb_claim_interface(handle, interface_num);
    if (r < 0) {
        printf("Cannot claim interface: %s\n", libusb_strerror(r));
        printf("Trying without claiming...\n");
    } else {
        printf("Interface claimed successfully\n");
        
        if (ep_out >= 0) {
            // Try bulk transfers
            try_bulk_transfer(handle, ep_out, huawei_switch_msg, sizeof(huawei_switch_msg), "Huawei switch message 1");
            usleep(500000);
            try_bulk_transfer(handle, ep_out, huawei_switch_msg2, sizeof(huawei_switch_msg2), "Huawei switch message 2");
            usleep(500000);
            try_bulk_transfer(handle, ep_out, eject_msg, sizeof(eject_msg), "Eject message");
        } else {
            // Try common endpoints
            printf("\nNo bulk endpoint found, trying common endpoints...\n");
            int endpoints[] = {0x01, 0x02, 0x03, 0x04, 0x05};
            for (int i = 0; i < 5; i++) {
                int transferred;
                r = libusb_bulk_transfer(handle, endpoints[i], huawei_switch_msg, 
                                        sizeof(huawei_switch_msg), &transferred, 1000);
                if (r == 0) {
                    printf("Success on endpoint 0x%02x\n", endpoints[i]);
                    break;
                }
            }
        }
        
        libusb_release_interface(handle, interface_num);
    }
    
    // Try control transfers
    return try_control_transfer(handle);
}

void print_usage(const char *prog) {
    printf("Huawei Mode Switch (Universal)\n\n");
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  -p <PID>   Force specific product ID (hex, e.g. 14fe)\n");
    printf("  -l         List devices only, don't switch\n");
    printf("  -h         Show this help\n");
    printf("\nSupported ZeroCD PIDs:\n");
    for (int i = 0; zerocd_pids[i] != 0; i++) {
        printf("  0x%04x - %s\n", zerocd_pids[i], get_pid_name(zerocd_pids[i]));
    }
}

int main(int argc, char **argv) {
    libusb_context *ctx = NULL;
    libusb_device_handle *handle = NULL;
    int r;
    int list_only = 0;
    uint16_t force_pid = 0;
    uint16_t found_pid = 0;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) {
            list_only = 1;
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            i++;
            force_pid = (uint16_t)strtol(argv[i], NULL, 16);
        }
    }
    
    printf("=== Huawei Mode Switch (Universal) ===\n\n");
    
    r = libusb_init(&ctx);
    if (r < 0) {
        fprintf(stderr, "Failed to init libusb\n");
        return 1;
    }
    
    int found_zerocd, found_modem;
    scan_huawei_devices(ctx, &found_zerocd, &found_modem);
    
    if (list_only) {
        libusb_exit(ctx);
        return 0;
    }
    
    // Find device to switch
    if (force_pid != 0) {
        handle = libusb_open_device_with_vid_pid(ctx, HUAWEI_VENDOR_ID, force_pid);
        if (handle) {
            found_pid = force_pid;
        }
    } else {
        handle = find_zerocd_device(ctx, &found_pid);
    }
    
    if (!handle) {
        if (force_pid) {
            printf("\nDevice 12d1:%04x not found.\n", force_pid);
        } else if (found_modem > 0) {
            printf("\nNo ZeroCD device found. Device may already be in modem mode.\n");
        } else {
            printf("\nNo Huawei device found to switch.\n");
        }
        libusb_exit(ctx);
        return 1;
    }
    
    printf("\n");
    int switched = switch_device(ctx, handle, found_pid);
    
    if (!switched) {
        libusb_close(handle);
    }
    
    printf("\n=== Waiting for device to re-enumerate... ===\n");
    sleep(3);
    
    // Check result
    scan_huawei_devices(ctx, &found_zerocd, &found_modem);
    
    if (found_modem > 0) {
        printf("\n*** SUCCESS! Device is now in modem mode ***\n");
    } else if (found_zerocd > 0) {
        printf("\nDevice still in ZeroCD mode. Try running again or check USB connection.\n");
    }
    
    printf("\nNext steps:\n");
    printf("  ./huawei_at -l              # List devices\n");
    printf("  ./huawei_at \"ATI\"           # Get modem info\n");
    printf("  ls /dev/tty.* /dev/cu.*     # Check serial ports\n");
    
    libusb_exit(ctx);
    return 0;
}

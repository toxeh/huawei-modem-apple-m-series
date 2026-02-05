/*
 * Huawei AT Command Tool (Universal)
 * Supports multiple Huawei modem PIDs: 0x1003, 0x1506, etc.
 * 
 * Usage: huawei_at "AT+CPIN?"
 *        huawei_at -p 1506 "ATI"    (force specific PID)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>

#define HUAWEI_VENDOR_ID    0x12D1
#define TIMEOUT_MS          2000
#define READ_TIMEOUT_MS     500
#define MAX_RESPONSE_SIZE   4096

// Supported modem PIDs (in order of priority)
// Includes all known Huawei USB modems in modem/network mode
static uint16_t supported_pids[] = {
    // === Classic 3G/HSPA Modems ===
    0x1001,  // E169/E620/E800/E1550 HSDPA Modem
    0x1003,  // E1550 Modem mode
    0x140c,  // E180/E1550 Modem
    0x1406,  // E1750 Modem
    0x1436,  // E173/E1750 Modem
    0x1465,  // K3765 HSPA Modem
    0x14AC,  // E1820 Modem
    0x14C6,  // K4605 Modem
    0x14C9,  // K4505 HSPA+ Modem
    0x1c05,  // E173 Modem
    0x1c07,  // E173s Modem
    0x1c1b,  // E3531 Modem
    
    // === E3xx Series (3G/4G) ===
    0x1506,  // E303/E3131/MS2372 Modem mode
    0x14db,  // E3131/E353 HiLink mode
    0x14fe,  // E303/E3131 Intermediate/Storage
    0x15ca,  // E3131h-2 Modem
    0x1f01,  // E353/E3131 (initial/ZeroCD, but sometimes modem)
    
    // === E3372/E8372 LTE Series ===
    0x1442,  // E3372 Stick/Modem mode
    0x14dc,  // E3372/E8372 HiLink mode
    0x155e,  // E8372 Stick/NCM mode
    0x157f,  // E8372 alternate mode
    0x1592,  // E8372h mode
    
    // === K-Series LTE Modems ===
    0x1505,  // E398/K5005 LTE Modem
    0x1520,  // K3765 HSPA
    0x1521,  // K4505 HSPA+
    0x1575,  // K5150 LTE Modem
    0x15c1,  // ME906s LTE M.2 Module
    
    // === Mobile WiFi (USB tethering mode) ===
    0x1f1e,  // K5160 initial
    
    // === Legacy/Other ===
    0x1404,  // E1752 Modem
    0x1411,  // E510 Modem
    0x141b,  // E1752 alternate
    0x1446,  // E1756/E173 (sometimes modem)
    0x1464,  // K4510/K4511 Modem
    0x14ba,  // E173 alternate
    0x14d1,  // E173 mode
    0x1c0b,  // E173s (modem off state)
    0x1da1,  // E3372 (some variants)
    0
};

static libusb_device_handle *handle = NULL;
static int ep_in = -1;
static int ep_out = -1;
static int claimed_interface = -1;

const char* get_pid_name(uint16_t pid) {
    switch(pid) {
        // Classic 3G/HSPA
        case 0x1001: return "E169/E620/E800/E1550";
        case 0x1003: return "E1550 Modem";
        case 0x140c: return "E180/E1550";
        case 0x1406: return "E1750";
        case 0x1436: return "E173/E1750";
        case 0x1446: return "E1756/E173";
        case 0x1465: return "K3765";
        case 0x14AC: return "E1820";
        case 0x14C6: return "K4605";
        case 0x14C9: return "K4505";
        case 0x1c05: return "E173";
        case 0x1c07: return "E173s";
        case 0x1c1b: return "E3531";
        // E3xx Series
        case 0x1506: return "E303/E3131/MS2372";
        case 0x14db: return "E3131/E353 HiLink";
        case 0x14fe: return "E303/E3131 Intermediate";
        case 0x15ca: return "E3131h-2";
        case 0x1f01: return "E353/E3131";
        // E3372/E8372 LTE
        case 0x1442: return "E3372 Stick";
        case 0x14dc: return "E3372/E8372 HiLink";
        case 0x155e: return "E8372 NCM";
        case 0x157f: return "E8372 Alt";
        case 0x1592: return "E8372h";
        // K-Series LTE
        case 0x1505: return "E398/K5005 LTE";
        case 0x1520: return "K3765 HSPA";
        case 0x1521: return "K4505 HSPA+";
        case 0x1575: return "K5150 LTE";
        case 0x15c1: return "ME906s LTE";
        case 0x1f1e: return "K5160";
        // Legacy/Other
        case 0x1404: return "E1752";
        case 0x1411: return "E510";
        case 0x141b: return "E1752 Alt";
        case 0x1464: return "K4510/K4511";
        case 0x14ba: return "E173 Alt";
        case 0x14d1: return "E173";
        case 0x1c0b: return "E173s Off";
        case 0x1da1: return "E3372";
        default: return "Unknown Huawei";
    }
}

int find_endpoints(libusb_device *dev) {
    struct libusb_config_descriptor *config;
    int r = libusb_get_active_config_descriptor(dev, &config);
    if (r < 0) return r;
    
    // First pass: look for CDC/Modem class interface
    for (int i = 0; i < config->bNumInterfaces; i++) {
        const struct libusb_interface *iface = &config->interface[i];
        for (int j = 0; j < iface->num_altsetting; j++) {
            const struct libusb_interface_descriptor *setting = &iface->altsetting[j];
            
            // Prefer CDC Data (0x0A) or Vendor Specific (0xFF) with bulk endpoints
            if (setting->bInterfaceClass != 0x0A && 
                setting->bInterfaceClass != 0xFF &&
                setting->bInterfaceClass != 0x02) {
                continue;
            }
            
            int found_in = -1, found_out = -1;
            
            for (int k = 0; k < setting->bNumEndpoints; k++) {
                const struct libusb_endpoint_descriptor *ep = &setting->endpoint[k];
                if ((ep->bmAttributes & 0x03) == LIBUSB_TRANSFER_TYPE_BULK) {
                    if (ep->bEndpointAddress & 0x80) {
                        found_in = ep->bEndpointAddress;
                    } else {
                        found_out = ep->bEndpointAddress;
                    }
                }
            }
            
            if (found_in >= 0 && found_out >= 0) {
                ep_in = found_in;
                ep_out = found_out;
                claimed_interface = setting->bInterfaceNumber;
                libusb_free_config_descriptor(config);
                return 0;
            }
        }
    }
    
    // Second pass: any interface with bulk endpoints
    for (int i = 0; i < config->bNumInterfaces; i++) {
        const struct libusb_interface *iface = &config->interface[i];
        for (int j = 0; j < iface->num_altsetting; j++) {
            const struct libusb_interface_descriptor *setting = &iface->altsetting[j];
            
            int found_in = -1, found_out = -1;
            
            for (int k = 0; k < setting->bNumEndpoints; k++) {
                const struct libusb_endpoint_descriptor *ep = &setting->endpoint[k];
                if ((ep->bmAttributes & 0x03) == LIBUSB_TRANSFER_TYPE_BULK) {
                    if (ep->bEndpointAddress & 0x80) {
                        found_in = ep->bEndpointAddress;
                    } else {
                        found_out = ep->bEndpointAddress;
                    }
                }
            }
            
            if (found_in >= 0 && found_out >= 0 && ep_in < 0) {
                ep_in = found_in;
                ep_out = found_out;
                claimed_interface = setting->bInterfaceNumber;
            }
        }
    }
    
    libusb_free_config_descriptor(config);
    return (ep_in >= 0 && ep_out >= 0) ? 0 : -1;
}

libusb_device_handle* find_huawei_modem(libusb_context *ctx, uint16_t force_pid, uint16_t *found_pid) {
    libusb_device_handle *h = NULL;
    
    // If specific PID requested, try only that
    if (force_pid != 0) {
        h = libusb_open_device_with_vid_pid(ctx, HUAWEI_VENDOR_ID, force_pid);
        if (h) {
            *found_pid = force_pid;
            return h;
        }
        return NULL;
    }
    
    // Try supported PIDs in order
    for (int i = 0; supported_pids[i] != 0; i++) {
        h = libusb_open_device_with_vid_pid(ctx, HUAWEI_VENDOR_ID, supported_pids[i]);
        if (h) {
            *found_pid = supported_pids[i];
            return h;
        }
    }
    
    return NULL;
}

void scan_huawei_devices(libusb_context *ctx) {
    libusb_device **devs;
    ssize_t cnt = libusb_get_device_list(ctx, &devs);
    
    fprintf(stderr, "\nAvailable Huawei devices:\n");
    int found = 0;
    
    for (ssize_t i = 0; i < cnt; i++) {
        struct libusb_device_descriptor desc;
        libusb_get_device_descriptor(devs[i], &desc);
        if (desc.idVendor == HUAWEI_VENDOR_ID) {
            fprintf(stderr, "  12d1:%04x - %s\n", desc.idProduct, get_pid_name(desc.idProduct));
            found++;
        }
    }
    
    if (!found) {
        fprintf(stderr, "  No Huawei devices found\n");
    }
    
    libusb_free_device_list(devs, 1);
}

int send_command(const char *cmd, char *response, size_t response_size) {
    int transferred;
    char buf[256];
    unsigned char read_buf[512];
    int r;
    size_t total_read = 0;
    
    // Prepare command with CR
    snprintf(buf, sizeof(buf), "%s\r", cmd);
    
    // Send command
    r = libusb_bulk_transfer(handle, ep_out, (unsigned char*)buf, strlen(buf), &transferred, TIMEOUT_MS);
    if (r != 0) {
        fprintf(stderr, "Error sending command: %s\n", libusb_strerror(r));
        return -1;
    }
    
    // Read response
    response[0] = '\0';
    int empty_reads = 0;
    
    while (total_read < response_size - 1 && empty_reads < 5) {
        r = libusb_bulk_transfer(handle, ep_in, read_buf, sizeof(read_buf) - 1, &transferred, READ_TIMEOUT_MS);
        
        if (r == LIBUSB_ERROR_TIMEOUT) {
            empty_reads++;
            continue;
        }
        
        if (r != 0) {
            break;
        }
        
        if (transferred > 0) {
            read_buf[transferred] = '\0';
            
            size_t to_copy = transferred;
            if (total_read + to_copy >= response_size - 1) {
                to_copy = response_size - 1 - total_read;
            }
            memcpy(response + total_read, read_buf, to_copy);
            total_read += to_copy;
            response[total_read] = '\0';
            
            // Check for final response markers
            if (strstr(response, "\r\nOK\r\n") || 
                strstr(response, "\r\nERROR\r\n") ||
                strstr(response, "\r\n+CME ERROR:") ||
                strstr(response, "\r\n+CMS ERROR:")) {
                break;
            }
            
            empty_reads = 0;
        } else {
            empty_reads++;
        }
    }
    
    return (int)total_read;
}

void print_usage(const char *prog) {
    fprintf(stderr, "Huawei AT Command Tool (Universal)\n\n");
    fprintf(stderr, "Usage: %s [options] <AT command>\n\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -p <PID>   Force specific product ID (hex, e.g. 1506)\n");
    fprintf(stderr, "  -r         Raw mode - no output processing\n");
    fprintf(stderr, "  -l         List available Huawei devices\n");
    fprintf(stderr, "  -v         Verbose mode\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s AT\n", prog);
    fprintf(stderr, "  %s \"AT+CPIN?\"\n", prog);
    fprintf(stderr, "  %s -p 1506 \"ATI\"\n", prog);
    fprintf(stderr, "  %s -l\n", prog);
}

int main(int argc, char **argv) {
    libusb_context *ctx = NULL;
    libusb_device *dev;
    char response[MAX_RESPONSE_SIZE];
    int r;
    int raw_mode = 0;
    int verbose = 0;
    int list_only = 0;
    uint16_t force_pid = 0;
    uint16_t found_pid = 0;
    const char *command = NULL;
    
    // Parse arguments
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0) {
            raw_mode = 1;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-l") == 0) {
            list_only = 1;
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            i++;
            force_pid = (uint16_t)strtol(argv[i], NULL, 16);
        } else if (argv[i][0] != '-') {
            command = argv[i];
            break;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }
    
    if (!list_only && !command) {
        print_usage(argv[0]);
        return 1;
    }
    
    r = libusb_init(&ctx);
    if (r < 0) {
        fprintf(stderr, "Failed to init libusb\n");
        return 1;
    }
    
    if (list_only) {
        scan_huawei_devices(ctx);
        libusb_exit(ctx);
        return 0;
    }
    
    handle = find_huawei_modem(ctx, force_pid, &found_pid);
    if (!handle) {
        if (force_pid) {
            fprintf(stderr, "Device 12d1:%04x not found.\n", force_pid);
        } else {
            fprintf(stderr, "No supported Huawei modem found.\n");
        }
        scan_huawei_devices(ctx);
        libusb_exit(ctx);
        return 1;
    }
    
    if (verbose) {
        fprintf(stderr, "Using device 12d1:%04x (%s)\n", found_pid, get_pid_name(found_pid));
    }
    
    dev = libusb_get_device(handle);
    
    if (find_endpoints(dev) < 0) {
        fprintf(stderr, "Could not find endpoints\n");
        libusb_close(handle);
        libusb_exit(ctx);
        return 1;
    }
    
    if (verbose) {
        fprintf(stderr, "Endpoints: IN=0x%02x OUT=0x%02x Interface=%d\n", ep_in, ep_out, claimed_interface);
    }
    
    // Detach kernel driver
    for (int j = 0; j < 8; j++) {
        if (libusb_kernel_driver_active(handle, j) == 1) {
            libusb_detach_kernel_driver(handle, j);
        }
    }
    
    r = libusb_claim_interface(handle, claimed_interface);
    if (r < 0 && verbose) {
        fprintf(stderr, "Warning: could not claim interface %d: %s\n", claimed_interface, libusb_strerror(r));
    }
    
    // Send command and get response
    r = send_command(command, response, sizeof(response));
    
    if (r > 0) {
        if (raw_mode) {
            printf("%s", response);
        } else {
            // Clean up response
            char *p = response;
            
            // Skip echo of command
            char *echo_end = strstr(p, "\r\n");
            if (echo_end && (echo_end - p) <= (int)strlen(command) + 2) {
                p = echo_end + 2;
            }
            
            printf("%s", p);
            
            size_t len = strlen(p);
            if (len > 0 && p[len-1] != '\n') {
                printf("\n");
            }
        }
    } else {
        fprintf(stderr, "No response\n");
    }
    
    libusb_release_interface(handle, claimed_interface);
    libusb_close(handle);
    libusb_exit(ctx);
    
    return 0;
}

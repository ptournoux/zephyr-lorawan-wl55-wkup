/*
 * Class A LoRaWAN sample application
 *
 * Copyright (c) 2020 Manivannan Sadhasivam <mani@kernel.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/lorawan/lorawan.h>
#include <zephyr/kernel.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/dt-bindings/gpio/stm32-gpio.h>


#include <zephyr/settings/settings.h>



// Variable to hold the dev_nonce in RAM
static uint16_t lorawan_dev_nonce; // LoRaWAN dev_nonce is typically uint16_t but can go up to 2^16-1.
                                 // The LoRaWAN MAC layer in Zephyr might handle the actual dev_nonce.
                                 // What you often need to save is the *frame counter* for persisted sessions,
                                 // or ensure the MAC layer itself persists its dev_nonce.
                                 // Let's assume you're managing it directly for this example,
                                 // or you're saving a related parameter that the stack uses.

// For this example, let's assume we're directly managing and saving the dev_nonce.
// In practice, you'd interface this with the Zephyr LoRaWAN stack's mechanism.
// The Zephyr LoRaWAN stack *should* ideally handle persisting its internal dev_nonce
// if NVS/Settings is enabled. This example shows how you *could* do it manually.

static int lorawan_settings_set(const char *name, size_t len_rd, settings_read_cb read_cb, void *cb_arg) {
    const char *next;
    int rc;

    if (settings_name_steq(name, "dev_nonce", &next) && !next) {
        if (len_rd == 0) { // Key found but no data (should not happen for scalar)
            printk("Error: dev_nonce read with no data\n");
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &lorawan_dev_nonce, sizeof(lorawan_dev_nonce));
        if (rc < 0) {
            printk("Error reading dev_nonce: %d\n", rc);
            return rc;
        }
        printk("SETTINGS: Loaded dev_nonce: %u\n", lorawan_dev_nonce);
        return 0;
    }
    return -ENOENT; // Setting not found
}


// Define the settings handler
SETTINGS_STATIC_HANDLER_DEFINE(
    lorawan_creds,             // Name of the settings group
    "lorawan",                 // Root name in the settings tree (e.g., "lorawan/dev_nonce")
    NULL,                      // Optional get function (can be NULL if direct variable access is ok)
    lorawan_settings_set,      // Set function (called when loading from NVS)
    NULL,                      // Optional commit function (called after all settings in a group are set)
    NULL                       // Optional export function (called when saving all settings)
);


// Function to initialize settings and load stored dev_nonce
void lorawan_settings_init(void) {
    int rc;
    rc = settings_subsys_init();
    if (rc) {
        printk("ERROR: settings_subsys_init failed (rc %d)\n", rc);
        return;
    }

    // Load the specific setting. This will call lorawan_settings_set if "lorawan/dev_nonce" exists.
    rc = settings_load_subtree("lorawan");
    if (rc == -ENOENT) {
        printk("SETTINGS: No lorawan settings found in NVS. Using defaults.\n");
        lorawan_dev_nonce = 0; // Default if not found
    } else if (rc) {
        printk("ERROR: Failed to load lorawan settings (rc %d)\n", rc);
    }

    printk("SETTINGS: Initial dev_nonce: %u\n", lorawan_dev_nonce);
}

// Function to save the current dev_nonce
void lorawan_settings_save_dev_nonce(uint16_t new_dev_nonce) {
    lorawan_dev_nonce = new_dev_nonce;
    int rc = settings_save_one("lorawan/dev_nonce", &lorawan_dev_nonce, sizeof(lorawan_dev_nonce));
    if (rc) {
        printk("ERROR: Failed to save dev_nonce: %d\n", rc);
    } else {
        printk("SETTINGS: Saved dev_nonce: %u\n", lorawan_dev_nonce);
    }
}


uint16_t lorawan_get_next_dev_nonce(void) {
    return lorawan_dev_nonce;
}

void lorawan_increment_and_save_dev_nonce(void) {
    lorawan_dev_nonce++; // Or however your stack provides the next nonce
    lorawan_settings_save_dev_nonce(lorawan_dev_nonce);
}

#define WAIT_TIME_US 4000000

#define WKUP_SRC_NODE DT_ALIAS(wkup_src)
#if !DT_NODE_HAS_STATUS_OKAY(WKUP_SRC_NODE)
#error "Unsupported board: wkup_src devicetree alias is not defined"
#endif

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(WKUP_SRC_NODE, gpios);
static const struct gpio_dt_spec led_0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led_2 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

/* Customize based on network configuration */
#define LORAWAN_DEV_EUI			{ 0x00, 0x80, 0xE1, 0x15, 0x05, 0x31, 0x08, 0x37 }
#define LORAWAN_JOIN_EUI		{ 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,\
					  0x01, 0x01 }
#define LORAWAN_APP_KEY			{ 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE,\
					  0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88,\
					  0x09, 0xCF, 0x4F, 0x3C }

#define DELAY K_MSEC(120000)



#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lorawan_class_a);

char data[] = {'h', 'e', 'l', 'l', 'o', 'w', 'o', 'r', 'l', 'd'};

static void dl_callback(uint8_t port, uint8_t flags, int16_t rssi, int8_t snr, uint8_t len,
			const uint8_t *hex_data)
{
	LOG_INF("Port %d, Pending %d, RSSI %ddB, SNR %ddBm, Time %d", port,
		flags & LORAWAN_DATA_PENDING, rssi, snr, !!(flags & LORAWAN_TIME_UPDATED));
	if (hex_data) {
		LOG_HEXDUMP_INF(hex_data, len, "Payload: ");
	}
}

static void lorwan_datarate_changed(enum lorawan_datarate dr)
{
	uint8_t unused, max_size;

	lorawan_get_payload_sizes(&unused, &max_size);
	LOG_INF("New Datarate: DR_%d, Max Payload %d", dr, max_size);
}

int main(void)
{

	__ASSERT_NO_MSG(gpio_is_ready_dt(&led_0));
	gpio_pin_configure_dt(&led_0, GPIO_OUTPUT_ACTIVE);
	gpio_pin_set(led_0.port, led_0.pin, 1);

	const struct device *lora_dev;
	struct lorawan_join_config join_cfg;
	uint8_t dev_eui[] = LORAWAN_DEV_EUI;
	uint8_t join_eui[] = LORAWAN_JOIN_EUI;
	uint8_t app_key[] = LORAWAN_APP_KEY;
	int ret;

    /* Configure wakeup pin (wkup) as input and read its value */
    const struct gpio_dt_spec wkup = GPIO_DT_SPEC_GET(WKUP_SRC_NODE, gpios);
    if (!device_is_ready(wkup.port)) {
        LOG_ERR("Wakeup GPIO device not ready");
        return 0;
    }
    ret = gpio_pin_configure_dt(&wkup, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure wakeup pin: %d", ret);
        return 0;
    }
    int val = gpio_pin_get_dt(&wkup);
    if (val < 0) {
        LOG_ERR("Failed to read wakeup pin: %d", val);
        return 0;
    }
    LOG_INF("Wakeup pin value: %d", val);

	if(val == 0) {
		gpio_pin_set(led_0.port, led_0.pin, 0);
		/* Setup button GPIO pin as a source for exiting Poweroff */
		gpio_pin_configure_dt(&button, STM32_GPIO_WKUP);
		/* If the wakeup pin is low, enter low power mode */
		LOG_INF("Entering low power mode...");
		k_sleep(K_MSEC(WAIT_TIME_US / 1000));
		sys_poweroff();

	} else {
		LOG_INF("Wakeup pin is high, continuing...");
	}

	struct lorawan_downlink_cb downlink_cb = {
		.port = LW_RECV_PORT_ANY,
		.cb = dl_callback
	};

	lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
	if (!device_is_ready(lora_dev)) {
		LOG_ERR("%s: device not ready.", lora_dev->name);
		return 0;
	}

#if defined(CONFIG_LORAMAC_REGION_EU868)
	/* If more than one region Kconfig is selected, app should set region
	 * before calling lorawan_start()
	 */
	ret = lorawan_set_region(LORAWAN_REGION_EU868);
	if (ret < 0) {
		LOG_ERR("lorawan_set_region failed: %d", ret);
		return 0;
	}
#endif

	__ASSERT_NO_MSG(gpio_is_ready_dt(&led_1));
	gpio_pin_configure_dt(&led_1, GPIO_OUTPUT_ACTIVE);
	gpio_pin_set(led_1.port, led_1.pin, 1);

	ret = lorawan_start();
	if (ret < 0) {
		LOG_ERR("lorawan_start failed: %d", ret);
		return 0;
	}

	lorawan_register_downlink_callback(&downlink_cb);
	lorawan_register_dr_changed_callback(lorwan_datarate_changed);
	lorawan_settings_init();
	join_cfg.mode = LORAWAN_ACT_OTAA;
	join_cfg.dev_eui = dev_eui;
	join_cfg.otaa.join_eui = join_eui;
	join_cfg.otaa.app_key = app_key;
	join_cfg.otaa.nwk_key = app_key;

	int joined = 10;
	while(joined > 0) {
		join_cfg.otaa.dev_nonce = lorawan_get_next_dev_nonce();
		LOG_INF("Joining network over OTAA ... attempt %d nonce %u", 10 - joined,join_cfg.otaa.dev_nonce);
		ret = lorawan_join(&join_cfg);
		if (ret < 0) {
			LOG_ERR("lorawan_join_network failed: %d continue", ret);
		} else {
			break;
		}

		k_sleep(K_MSEC(1000));
		joined--;
		lorawan_increment_and_save_dev_nonce();
		LOG_INF("Waiting for join... %d", joined);
	}

	if (ret < 0) {
		LOG_ERR("lorawan_join_network failed: %d", ret);
		gpio_pin_set(led_2.port, led_2.pin, 1);
			return 0;
	}


	gpio_pin_set(led_1.port, led_1.pin, 0);
	k_sleep(K_MSEC(1000));

	__ASSERT_NO_MSG(gpio_is_ready_dt(&led_2));
	gpio_pin_configure_dt(&led_2, GPIO_OUTPUT_ACTIVE);
	while (1) {

		/* Configure wakeup pin (wkup) as input and read its value */
		/* This allows to rearm the device without unplugging the battery */
		gpio_pin_set(led_2.port, led_2.pin, 1);
		ret = gpio_pin_configure_dt(&wkup, GPIO_INPUT);
		if (ret < 0) {
	        LOG_ERR("Failed to configure wakeup pin: %d", ret);
			return 0;
		}
		int val = gpio_pin_get_dt(&wkup);
    	if (val < 0) {
	        LOG_ERR("Failed to read wakeup pin: %d", val);
        	return 0;
    	}
		data[0] = (uint8_t) val;
		gpio_pin_set(led_1.port, led_1.pin, val);







		LOG_INF("Sending data...");
		/* Send the wakeup pin value as the first byte of the payload
		It allows the backend to determine if this has stopped because the 
		device was rearmed */
    	LOG_INF("Wakeup pin value: %d", val);
		ret = lorawan_send(2, data, sizeof(char),
				   LORAWAN_MSG_CONFIRMED);

		/*
		 * Note: The stack may return -EAGAIN if the provided data
		 * length exceeds the maximum possible one for the region and
		 * datarate. But since we are just sending the same data here,
		 * we'll just continue.
		 */
		if (ret == -EAGAIN) {
			LOG_ERR("lorawan_send failed: %d. Continuing...", ret);
			k_sleep(DELAY);
			continue;
		} else {}

		if (ret < 0) {
			LOG_ERR("lorawan_send failed: %d", ret);
		}

		LOG_INF("Data sent!");










		/* If GPIO was low, go back to sleep */
		if(val == 0) {
			gpio_pin_set(led_2.port, led_2.pin, 0);
			gpio_pin_set(led_1.port, led_1.pin, 0);
			gpio_pin_set(led_0.port, led_0.pin, 0);
			/* Setup button GPIO pin as a source for exiting Poweroff */
			gpio_pin_configure_dt(&button, STM32_GPIO_WKUP);
			/* If the wakeup pin is low, enter low power mode */
			LOG_INF("Entering low power mode...");
			k_sleep(K_MSEC(WAIT_TIME_US / 1000));
			sys_poweroff();
		} else {
			LOG_INF("Wakeup pin is high, continuing...");
		}
		gpio_pin_set(led_2.port, led_2.pin, 0);
		gpio_pin_set(led_1.port, led_1.pin, 0);
		gpio_pin_set(led_0.port, led_0.pin, 0);
		k_sleep(DELAY);
	}
}

#include <zephyr.h>
#include <logging/log.h>
#include <device.h>
#include <zephyr/kernel.h>
#include <drivers/display.h>
#include <lvgl.h>

LOG_MODULE_REGISTER(app);

void main(void)
{
    uint32_t count = 0U;
    char count_str[11] = {0};
    const struct device *display_dev;
    lv_obj_t *hello_world_label;
    lv_obj_t *count_label;

    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        LOG_ERR("Device not ready, aborting test");
        return;
    }

    lv_obj_t *hello_world_button;

    hello_world_button = lv_btn_create(lv_scr_act());
    lv_obj_align(hello_world_button, LV_ALIGN_CENTER, 0, 0);
    hello_world_label = lv_label_create(hello_world_button);

    lv_label_set_text(hello_world_label, "Hello world!");
    lv_obj_align(hello_world_label, LV_ALIGN_CENTER, 0, 0);

    count_label = lv_label_create(lv_scr_act());
    lv_obj_align(count_label, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_task_handler();
    display_blanking_off(display_dev);

    while (1) {
        if ((count % 100) == 0U) {
            sprintf(count_str, "%d", count/100U);
            lv_label_set_text(count_label, count_str);
        }
        lv_task_handler();
        k_sleep(K_MSEC(10));
        ++count;
    }
}

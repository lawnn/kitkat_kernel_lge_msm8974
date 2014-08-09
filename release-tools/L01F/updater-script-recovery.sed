assert(getprop("ro.product.device") == "l01f" || getprop("ro.build.product") == "l01f" || 
       getprop("ro.product.device") == "g2" || getprop("ro.build.product") == "g2" || 
       getprop("ro.product.device") == "galbi" || getprop("ro.build.product") == "galbi" || abort("This package is for \"l01f,g2,galbi\" devices; this is a \"" + getprop("ro.product.device") + "\"."););
ui_print("");
ui_print("");
ui_print("------------------------------------------------");
ui_print("@VERSION");
ui_print("  KBC Developers:");
ui_print("    Scotty");
ui_print("    HomuHomu");
ui_print("    ma34s");
ui_print("    sakuramilk");
ui_print("    lawn");
ui_print("    sakuraba001");
ui_print("    blackzigong");
ui_print("------------------------------------------------");
ui_print("");
show_progress(0.500000, 0);

ui_print("flashing recovery image...");
package_extract_file("recovery.img", "/dev/block/platform/msm_sdcc.1/by-name/recovery");
show_progress(0.100000, 0);

run_program("/mkdir","/tmp/loki");
package_extract_dir("loki", "/tmp/loki");
set_perm(0, 0, 0777, "/tmp/loki/loki_flash");
set_perm(0, 0, 0777, "/tmp/loki/loki_patch");
set_perm(0, 0, 0777, "/tmp/loki/loki.sh");

show_progress(0.700000, 0);
assert(run_program("/tmp/loki/loki.sh","recovery") == 0);

show_progress(0.100000, 0);

ui_print("flash complete. Enjoy!");
set_progress(1.000000);

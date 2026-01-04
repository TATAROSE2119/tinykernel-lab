#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

__visible struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0x7cbe2261, __VMLINUX_SYMBOL_STR(module_layout) },
	{ 0xf4d71332, __VMLINUX_SYMBOL_STR(platform_driver_unregister) },
	{ 0x43fd5a10, __VMLINUX_SYMBOL_STR(__platform_driver_register) },
	{ 0xfb9d281e, __VMLINUX_SYMBOL_STR(gpiod_direction_output_raw) },
	{ 0x47229b5c, __VMLINUX_SYMBOL_STR(gpio_request) },
	{ 0xde6fb46c, __VMLINUX_SYMBOL_STR(of_get_named_gpio_flags) },
	{ 0x76cbf004, __VMLINUX_SYMBOL_STR(of_find_node_opts_by_path) },
	{ 0xc9e32eaa, __VMLINUX_SYMBOL_STR(device_create) },
	{ 0x98d0cd61, __VMLINUX_SYMBOL_STR(__class_create) },
	{ 0xb9890d5a, __VMLINUX_SYMBOL_STR(cdev_add) },
	{ 0xb94f81d5, __VMLINUX_SYMBOL_STR(cdev_init) },
	{ 0x29537c9e, __VMLINUX_SYMBOL_STR(alloc_chrdev_region) },
	{ 0xfe990052, __VMLINUX_SYMBOL_STR(gpio_free) },
	{ 0x303127b1, __VMLINUX_SYMBOL_STR(class_destroy) },
	{ 0x671ee494, __VMLINUX_SYMBOL_STR(device_destroy) },
	{ 0x7485e15e, __VMLINUX_SYMBOL_STR(unregister_chrdev_region) },
	{ 0x156efefc, __VMLINUX_SYMBOL_STR(cdev_del) },
	{ 0xfa2a45e, __VMLINUX_SYMBOL_STR(__memzero) },
	{ 0xfbc74f64, __VMLINUX_SYMBOL_STR(__copy_from_user) },
	{ 0xa60d515b, __VMLINUX_SYMBOL_STR(gpiod_set_raw_value) },
	{ 0xe11c84ad, __VMLINUX_SYMBOL_STR(gpio_to_desc) },
	{ 0x2e5810c6, __VMLINUX_SYMBOL_STR(__aeabi_unwind_cpp_pr1) },
	{ 0x27e1a049, __VMLINUX_SYMBOL_STR(printk) },
	{ 0xb1ad28e0, __VMLINUX_SYMBOL_STR(__gnu_mcount_nc) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "D9847E900F2922CEAD15692");

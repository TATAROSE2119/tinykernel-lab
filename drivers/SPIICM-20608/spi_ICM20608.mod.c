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
	{ 0x27ffd4cb, __VMLINUX_SYMBOL_STR(driver_unregister) },
	{ 0x643f5980, __VMLINUX_SYMBOL_STR(spi_register_driver) },
	{ 0x67c2fa54, __VMLINUX_SYMBOL_STR(__copy_to_user) },
	{ 0x49317872, __VMLINUX_SYMBOL_STR(dev_err) },
	{ 0x62bb6094, __VMLINUX_SYMBOL_STR(spi_setup) },
	{ 0xde6fb46c, __VMLINUX_SYMBOL_STR(of_get_named_gpio_flags) },
	{ 0x69da9d32, __VMLINUX_SYMBOL_STR(of_get_parent) },
	{ 0xc9e32eaa, __VMLINUX_SYMBOL_STR(device_create) },
	{ 0x98d0cd61, __VMLINUX_SYMBOL_STR(__class_create) },
	{ 0xb9890d5a, __VMLINUX_SYMBOL_STR(cdev_add) },
	{ 0xb94f81d5, __VMLINUX_SYMBOL_STR(cdev_init) },
	{ 0x29537c9e, __VMLINUX_SYMBOL_STR(alloc_chrdev_region) },
	{ 0x8e865d3c, __VMLINUX_SYMBOL_STR(arm_delay_ops) },
	{ 0xfe990052, __VMLINUX_SYMBOL_STR(gpio_free) },
	{ 0x303127b1, __VMLINUX_SYMBOL_STR(class_destroy) },
	{ 0x671ee494, __VMLINUX_SYMBOL_STR(device_destroy) },
	{ 0x7485e15e, __VMLINUX_SYMBOL_STR(unregister_chrdev_region) },
	{ 0x156efefc, __VMLINUX_SYMBOL_STR(cdev_del) },
	{ 0x1bffe2ec, __VMLINUX_SYMBOL_STR(spi_sync) },
	{ 0xfa2a45e, __VMLINUX_SYMBOL_STR(__memzero) },
	{ 0xa60d515b, __VMLINUX_SYMBOL_STR(gpiod_set_raw_value) },
	{ 0xe11c84ad, __VMLINUX_SYMBOL_STR(gpio_to_desc) },
	{ 0x5f754e5a, __VMLINUX_SYMBOL_STR(memset) },
	{ 0x2e5810c6, __VMLINUX_SYMBOL_STR(__aeabi_unwind_cpp_pr1) },
	{ 0xb6196860, __VMLINUX_SYMBOL_STR(_dev_info) },
	{ 0xb1ad28e0, __VMLINUX_SYMBOL_STR(__gnu_mcount_nc) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "C5D37FC6E5257476E6B071D");

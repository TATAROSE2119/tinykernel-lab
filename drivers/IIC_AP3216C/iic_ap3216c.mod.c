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
	{ 0x4e087c5, __VMLINUX_SYMBOL_STR(i2c_del_driver) },
	{ 0x4f11679a, __VMLINUX_SYMBOL_STR(i2c_register_driver) },
	{ 0x67c2fa54, __VMLINUX_SYMBOL_STR(__copy_to_user) },
	{ 0x8e865d3c, __VMLINUX_SYMBOL_STR(arm_delay_ops) },
	{ 0x619e1e87, __VMLINUX_SYMBOL_STR(i2c_get_adapter) },
	{ 0x7a270c8a, __VMLINUX_SYMBOL_STR(i2c_transfer) },
	{ 0xc9e32eaa, __VMLINUX_SYMBOL_STR(device_create) },
	{ 0x98d0cd61, __VMLINUX_SYMBOL_STR(__class_create) },
	{ 0xb9890d5a, __VMLINUX_SYMBOL_STR(cdev_add) },
	{ 0xb94f81d5, __VMLINUX_SYMBOL_STR(cdev_init) },
	{ 0x29537c9e, __VMLINUX_SYMBOL_STR(alloc_chrdev_region) },
	{ 0x303127b1, __VMLINUX_SYMBOL_STR(class_destroy) },
	{ 0x671ee494, __VMLINUX_SYMBOL_STR(device_destroy) },
	{ 0x7485e15e, __VMLINUX_SYMBOL_STR(unregister_chrdev_region) },
	{ 0x156efefc, __VMLINUX_SYMBOL_STR(cdev_del) },
	{ 0x2e5810c6, __VMLINUX_SYMBOL_STR(__aeabi_unwind_cpp_pr1) },
	{ 0x27e1a049, __VMLINUX_SYMBOL_STR(printk) },
	{ 0xb1ad28e0, __VMLINUX_SYMBOL_STR(__gnu_mcount_nc) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "1D3C105C814F16869F61611");

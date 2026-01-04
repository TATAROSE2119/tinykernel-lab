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
	{ 0x8e865d3c, __VMLINUX_SYMBOL_STR(arm_delay_ops) },
	{ 0x49317872, __VMLINUX_SYMBOL_STR(dev_err) },
	{ 0x1934f352, __VMLINUX_SYMBOL_STR(devm_iio_device_register) },
	{ 0x62bb6094, __VMLINUX_SYMBOL_STR(spi_setup) },
	{ 0x3766cdf4, __VMLINUX_SYMBOL_STR(devm_iio_device_alloc) },
	{ 0x1bffe2ec, __VMLINUX_SYMBOL_STR(spi_sync) },
	{ 0xfa2a45e, __VMLINUX_SYMBOL_STR(__memzero) },
	{ 0x5f754e5a, __VMLINUX_SYMBOL_STR(memset) },
	{ 0x987297f6, __VMLINUX_SYMBOL_STR(spi_write_then_read) },
	{ 0x2e5810c6, __VMLINUX_SYMBOL_STR(__aeabi_unwind_cpp_pr1) },
	{ 0xb6196860, __VMLINUX_SYMBOL_STR(_dev_info) },
	{ 0xb1ad28e0, __VMLINUX_SYMBOL_STR(__gnu_mcount_nc) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";

MODULE_ALIAS("of:N*T*Cinvn,icm20608*");

MODULE_INFO(srcversion, "5456466BB568415ADA24F51");

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
	{ 0x2c7bb5a8, __VMLINUX_SYMBOL_STR(put_disk) },
	{ 0x7cd51b1f, __VMLINUX_SYMBOL_STR(blk_cleanup_queue) },
	{ 0xfa233893, __VMLINUX_SYMBOL_STR(add_disk) },
	{ 0x6a5df82a, __VMLINUX_SYMBOL_STR(blk_queue_make_request) },
	{ 0x1d8d809e, __VMLINUX_SYMBOL_STR(del_gendisk) },
	{ 0x6373ba88, __VMLINUX_SYMBOL_STR(blk_alloc_queue) },
	{ 0xb5a459dc, __VMLINUX_SYMBOL_STR(unregister_blkdev) },
	{ 0x4adb88b9, __VMLINUX_SYMBOL_STR(alloc_disk) },
	{ 0x37a0cba, __VMLINUX_SYMBOL_STR(kfree) },
	{ 0x71a50dbc, __VMLINUX_SYMBOL_STR(register_blkdev) },
	{ 0x86a4889a, __VMLINUX_SYMBOL_STR(kmalloc_order_trace) },
	{ 0x1e047854, __VMLINUX_SYMBOL_STR(warn_slowpath_fmt) },
	{ 0xac5ee9b1, __VMLINUX_SYMBOL_STR(bio_endio) },
	{ 0x676bbc0f, __VMLINUX_SYMBOL_STR(_set_bit) },
	{ 0xc57a83ed, __VMLINUX_SYMBOL_STR(page_address) },
	{ 0x9d669763, __VMLINUX_SYMBOL_STR(memcpy) },
	{ 0x2e5810c6, __VMLINUX_SYMBOL_STR(__aeabi_unwind_cpp_pr1) },
	{ 0x27e1a049, __VMLINUX_SYMBOL_STR(printk) },
	{ 0xb1ad28e0, __VMLINUX_SYMBOL_STR(__gnu_mcount_nc) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "13138F099F6C5046474F595");

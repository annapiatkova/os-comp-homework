#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/timex.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anna Piatkova");
MODULE_DESCRIPTION("My keyboard: creates an input device that works as a keyboard typing in random letters every 5 seconds");
MODULE_VERSION("0.1");

static struct input_dev *my_keyboard;

unsigned int letter_number_to_letter_code(unsigned int n);

unsigned int letter_number_to_letter_code(unsigned int n) {
	switch (n) {
	case 0:
		return KEY_A;
	case 1:
		return KEY_B;
	case 2:
		return KEY_C;
	case 3:
		return KEY_D;
	case 4:
		return KEY_E;
	case 5:
		return KEY_F;
	case 6:
		return KEY_G;
	case 7:
		return KEY_H;
	case 8:
		return KEY_I;
	case 9:
		return KEY_J;
	case 10:
		return KEY_K;
	case 11:
		return KEY_L;
	case 12:
		return KEY_M;
	case 13:
		return KEY_N;
	case 14:
		return KEY_O;
	case 15:
		return KEY_P;
	case 16:
		return KEY_Q;
	case 17:
		return KEY_R;
	case 18:
		return KEY_S;
	case 19:
		return KEY_T;
	case 20:
		return KEY_U;
	case 21:
		return KEY_V;
	case 22:
		return KEY_W;
	case 23:
		return KEY_X;
	case 24:
		return KEY_Y;
	}
	return KEY_Z;
}

static void my_keyboard_poll(struct input_dev *dev) {
	unsigned int code = letter_number_to_letter_code(random_get_entropy_fallback() % 26);
	input_report_key(my_keyboard, code, 1);
	input_sync(my_keyboard);
	input_report_key(my_keyboard, code, 0);
	input_sync(my_keyboard);
	return;
}

static int __init my_keyboard_init(void)
{
	int ret;

	my_keyboard = input_allocate_device();
	if (!my_keyboard) {
		pr_err("my keyboard: not enough memory\n");
		return -ENOMEM;
	}

	my_keyboard->name = "my keyboard";
	my_keyboard->evbit[0] = BIT_MASK(EV_KEY);

	set_bit(KEY_A, my_keyboard->keybit);
	set_bit(KEY_B, my_keyboard->keybit);
	set_bit(KEY_C, my_keyboard->keybit);
	set_bit(KEY_D, my_keyboard->keybit);
	set_bit(KEY_E, my_keyboard->keybit);
	set_bit(KEY_F, my_keyboard->keybit);
	set_bit(KEY_G, my_keyboard->keybit);
	set_bit(KEY_H, my_keyboard->keybit);
	set_bit(KEY_I, my_keyboard->keybit);
	set_bit(KEY_J, my_keyboard->keybit);
	set_bit(KEY_K, my_keyboard->keybit);
	set_bit(KEY_L, my_keyboard->keybit);
	set_bit(KEY_M, my_keyboard->keybit);
	set_bit(KEY_N, my_keyboard->keybit);
	set_bit(KEY_O, my_keyboard->keybit);
	set_bit(KEY_P, my_keyboard->keybit);
	set_bit(KEY_Q, my_keyboard->keybit);
	set_bit(KEY_R, my_keyboard->keybit);
	set_bit(KEY_S, my_keyboard->keybit);
	set_bit(KEY_T, my_keyboard->keybit);
	set_bit(KEY_U, my_keyboard->keybit);
	set_bit(KEY_V, my_keyboard->keybit);
	set_bit(KEY_W, my_keyboard->keybit);
	set_bit(KEY_X, my_keyboard->keybit);
	set_bit(KEY_Y, my_keyboard->keybit);
	set_bit(KEY_Z, my_keyboard->keybit);

	ret = input_setup_polling(my_keyboard, my_keyboard_poll);
	if (ret) {
		pr_err("my keyboard: failed to set up polling\n");
		goto err_free_device;
	}

	input_set_poll_interval(my_keyboard, 5000);

	ret = input_register_device(my_keyboard);
	if (ret) {
		pr_err("my keyboard: failed to register device\n");
		goto err_free_device;
	}

	pr_info("my keyboard: module loaded\n");
	return 0;

err_free_device:
	input_free_device(my_keyboard);
	return ret;
}

static void __exit my_keyboard_exit(void) {
	input_unregister_device(my_keyboard);
	pr_info("my keyboard: module unloaded\n");
}

module_init(my_keyboard_init);
module_exit(my_keyboard_exit);
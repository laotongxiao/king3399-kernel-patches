#include <linux/module.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/interrupt.h>
// #include <linux/wakelock.h>

#define GPIO_NUM_MAX 40

#define GPIO_FUNCTION_OUTPUT 0
#define GPIO_FUNCTION_INPUT 1
#define GPIO_FUNCTION_IRQ 2
#define GPIO_FUNCTION_FLASH 3
#define GPIO_FUNCTION_OUTPUT_CTRL 4

static int flash_flag = 0;

struct rp_power {
	int gpio_num;		//gpui num
	int gpio_irq;
	int action;			//gpio flag
	int gpio_event;		//input only
	int send_mode;		//input only
	int gpio_function;	//gpio function,i/o
	int gpio_ctrl;
	char *gpio_name;
};

struct rp_power_data {
	struct rp_power rp_power_num[GPIO_NUM_MAX];
	struct input_dev *input;
	struct timer_list mytimer;
	int gpio_dts_num;
};

static struct rp_power_data *gpio_data = NULL;
static int event_flag = 0;
static int open_now = 0;
static char* file_name = NULL;

static int sleep_flag = 0;
// static struct wake_lock rp_wake_lock;


static int gpio_open(struct inode *inode, struct file *file)
{
	struct dentry* dent = file->f_path.dentry;
	int i = 0;

	file_name = (char*)(dent->d_name.name);

	for (i = 0; i < gpio_data->gpio_dts_num; i++){
		if(!strcmp(file_name,gpio_data->rp_power_num[i].gpio_name)){
			open_now = i;
		}
	}
	return 0;
}


static ssize_t gpio_write(struct file *file, const char *buffer,size_t count, loff_t *data)
{
	char buf[2]={0};
	char s1[]="1";
	
	if(copy_from_user(&buf[0],buffer,1)){
		printk("failed to copy data to kernel space\n");
		return -EFAULT;     
	}

	if(!strcmp(buf,s1)){
		gpio_direction_output(gpio_data->rp_power_num[open_now].gpio_num,1);
		printk("%s write 1 succeed\n",gpio_data->rp_power_num[open_now].gpio_name);
	}else{	
		gpio_direction_output(gpio_data->rp_power_num[open_now].gpio_num,0);
		printk("%s write 0 succeed\n",gpio_data->rp_power_num[open_now].gpio_name);
	}
	return count;
}


static ssize_t gpio_read(struct file *file, char __user * buffer, size_t count, loff_t *data)
{
	int gpio_val = 0;
	int len = 0;
	char s[10] = {0};

	if(*data)
		return 0;

	gpio_val = gpio_get_value(gpio_data->rp_power_num[open_now].gpio_num);
	printk("get %s value %d\n",gpio_data->rp_power_num[open_now].gpio_name,gpio_val);

	len = sprintf(s+len, "%d\n",gpio_val);	

	return simple_read_from_buffer(buffer, count, data, s, 2);
}


static const struct proc_ops gpio_ops = {
//	.owner          = THIS_MODULE,
	.proc_flags		= PROC_ENTRY_PERMANENT,
	.proc_open		= gpio_open,
	.proc_write		= gpio_write,
	.proc_read		= gpio_read,
};


//static void send_event(unsigned long data){
static void send_event(struct timer_list* list){
	int gpio_value = 0;
	int i = 0;
	for(i = 0; i <= gpio_data->gpio_dts_num; i++) {
		switch(gpio_data->rp_power_num[i].gpio_function) {
			case GPIO_FUNCTION_INPUT :
				gpio_value  = gpio_get_value(gpio_data->rp_power_num[i].gpio_num);
					
					if(gpio_value == 1){
						input_report_key(gpio_data->input, gpio_data->rp_power_num[i].gpio_event, 1);
						input_sync(gpio_data->input);
					}
					if(gpio_value == 0){
						input_report_key(gpio_data->input, gpio_data->rp_power_num[i].gpio_event, 0);
						input_sync(gpio_data->input);
					}
				
            //printk("\n%s gpio num %d  %d\n",__func__,gpio_data->rp_power_num[i].gpio_num,gpio_value);
            //printk("\n%s send event %d\n",__func__,gpio_data->rp_power_num[i].gpio_event);
				break;
			case GPIO_FUNCTION_FLASH :
				gpio_direction_output(gpio_data->rp_power_num[i].gpio_num,!flash_flag);
				flash_flag = !flash_flag;
				break;
		}
	}
		
	mod_timer(&(gpio_data->mytimer), jiffies + msecs_to_jiffies(2000));
}	


static int rp_power_probe(struct platform_device *pdev) {
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child_np;
	struct device *dev = &pdev->dev;
	static struct proc_dir_entry *root_entry_gpio;
	enum of_gpio_flags  gpio_flags;
	int ret = 0;
	int gpio_cnt = 0;	
	char gpio_name_num[GPIO_NUM_MAX];
	int gpio_in_cnt = 0;
	int cnt =0;

	gpio_data = devm_kzalloc(&pdev->dev, sizeof(struct rp_power_data),GFP_KERNEL);
	if (!gpio_data) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	gpio_data->gpio_dts_num = of_get_child_count(np);
        printk("rp_power prepare build %d gpio\n",gpio_data->gpio_dts_num);

    	if (gpio_data->gpio_dts_num == 0){
        	dev_info(&pdev->dev, "no gpio defined\n");
	}

	/* create node */
	root_entry_gpio = proc_mkdir("rp_power", NULL);
	
	for_each_child_of_node(np, child_np)
	{
		/* parse dts */
		gpio_data->rp_power_num[gpio_cnt].gpio_num = of_get_named_gpio_flags(child_np, "gpio_num", 0, &gpio_flags);
		if (!gpio_is_valid(gpio_data->rp_power_num[gpio_cnt].gpio_num)){
			return -1;
		}		

		gpio_data->rp_power_num[gpio_cnt].gpio_name = (char*)child_np -> name;
		gpio_data->rp_power_num[gpio_cnt].action = gpio_flags;
		gpio_data->rp_power_num[gpio_cnt].gpio_ctrl = gpio_cnt;
		of_property_read_u32(child_np, "gpio_function", &(gpio_data->rp_power_num[gpio_cnt].gpio_function));

		printk("rp_power request %s\n",gpio_data->rp_power_num[gpio_cnt].gpio_name);

		
		switch(gpio_data->rp_power_num[gpio_cnt].gpio_function) {
			case GPIO_FUNCTION_INPUT :		/* init input gpio */
				ret = gpio_request(gpio_data->rp_power_num[gpio_cnt].gpio_num, "gpio_num");
				if (ret < 0)
				{
					printk("gpio%d request error\n",gpio_data->rp_power_num[gpio_cnt].gpio_num);
				}else{
					printk("success request gpio %d in\n",gpio_data->rp_power_num[gpio_cnt].gpio_num);

					//gpio_direction_output(gpio_data->rp_power_num[gpio_cnt].gpio_num,!gpio_data->rp_power_num[gpio_cnt].action);
					gpio_direction_input(gpio_data->rp_power_num[gpio_cnt].gpio_num);
					event_flag = gpio_flags;
					of_property_read_u32(child_np, "send_mode", &(gpio_data->rp_power_num[gpio_cnt].send_mode));
					of_property_read_u32(child_np, "gpio_event", &(gpio_data->rp_power_num[gpio_cnt].gpio_event));
					gpio_in_cnt++;
				}
				break;

			case GPIO_FUNCTION_OUTPUT :		/* init output gpio */
				ret = gpio_request(gpio_data->rp_power_num[gpio_cnt].gpio_num, "gpio_num");
				if (ret < 0){
					printk("gpio%d request error\n",gpio_data->rp_power_num[gpio_cnt].gpio_num);
					//return ret;
				}else{
					gpio_direction_output(gpio_data->rp_power_num[gpio_cnt].gpio_num,!gpio_data->rp_power_num[gpio_cnt].action);
					printk("success request gpio%d out\n",gpio_data->rp_power_num[gpio_cnt].gpio_num);
				}
				break;
				
			case GPIO_FUNCTION_FLASH :
				ret = gpio_request(gpio_data->rp_power_num[gpio_cnt].gpio_num, "gpio_num");
				if (ret < 0){
					printk("gpio%d request error\n",gpio_data->rp_power_num[gpio_cnt].gpio_num);
					//return ret;
				}else{
					gpio_direction_output(gpio_data->rp_power_num[gpio_cnt].gpio_num,!gpio_data->rp_power_num[gpio_cnt].action);
					printk("success request gpio%d flash\n",gpio_data->rp_power_num[gpio_cnt].gpio_num);
					gpio_in_cnt++;

				}
				break;
				
			case GPIO_FUNCTION_OUTPUT_CTRL :		/* init output gpio for proc */
				ret = gpio_request(gpio_data->rp_power_num[gpio_cnt].gpio_num, "gpio_num");
				if (ret < 0){
					printk("gpio%d request error\n",gpio_data->rp_power_num[gpio_cnt].gpio_num);
					//return ret;
				}else{
					gpio_direction_output(gpio_data->rp_power_num[gpio_cnt].gpio_num,!gpio_data->rp_power_num[gpio_cnt].action);
					printk("success request gpio%d out\n",gpio_data->rp_power_num[gpio_cnt].gpio_num);
				}
				
				sprintf(gpio_name_num,gpio_data->rp_power_num[gpio_cnt].gpio_name,gpio_cnt);
				proc_create(gpio_name_num, 0666 , root_entry_gpio , &gpio_ops);
				
				break;
		}
		gpio_cnt++;
	}
	
	if (gpio_in_cnt > 0)
	{
		/* init timer */
		/* old linux version timer api */
		// init_timer(&(gpio_data->mytimer));
		// gpio_data->mytimer.expires = jiffies + msecs_to_jiffies(10000);
		// gpio_data->mytimer.function = send_event;
		// add_timer(&(gpio_data->mytimer));


		timer_setup(&(gpio_data->mytimer), send_event, 0);
		gpio_data->mytimer.expires = jiffies + msecs_to_jiffies(10000);
		add_timer(&(gpio_data->mytimer));
		
		/* init struct input_dev */ 
		gpio_data->input = devm_input_allocate_device(dev);
		gpio_data->input->name = "gpio_event";      /* pdev->name; */
		gpio_data->input->phys = "gpio_event/input1";
		gpio_data->input->dev.parent = dev;
		gpio_data->input->id.bustype = BUS_HOST;
		gpio_data->input->id.vendor = 0x0001;
		gpio_data->input->id.product = 0x0001;
		gpio_data->input->id.version = 0x0100;
		for(cnt = 0; cnt < gpio_cnt; cnt++){
			if (gpio_data->rp_power_num[cnt].gpio_function == 1){
				input_set_capability(gpio_data->input, EV_KEY, gpio_data->rp_power_num[cnt].gpio_event);
			}
		}
		ret = input_register_device(gpio_data->input);
	}
	
	of_property_read_u32(np, "rp_not_deep_sleep", &sleep_flag);
	// if(sleep_flag != 0){
	// 	printk("rpdzkj wake lock\n");
	// 	wake_lock_init(&rp_wake_lock,WAKE_LOCK_SUSPEND, "rpdzkj_no_deep_sleep");
	// 	wake_lock(&rp_wake_lock);
	// }
	
	platform_set_drvdata(pdev, gpio_data);	
	return 0;
}

static int rp_power_remove(struct platform_device *pdev)
{
    return 0;
}


static const struct of_device_id rp_power_of_match[] = {
    { .compatible = "rp_power" },
    { }
};

static struct platform_driver rp_power_driver = {
    .probe = rp_power_probe,
    .remove = rp_power_remove,
    .driver = {
                .name           = "rp_power",
                .of_match_table = of_match_ptr(rp_power_of_match),
        },
};

module_platform_driver(rp_power_driver);
MODULE_LICENSE("GPL");

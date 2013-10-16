#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/sched.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/timer.h>
#include <linux/types.h>	/* size_t */
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/hdreg.h>	/* HDIO_GETGEO */
#include <linux/kdev_t.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>	/* invalidate_bdev */
#include <linux/bio.h>
#include <linux/spinlock.h>
#include "tssd.h"

MODULE_LICENSE("Dual BSD/GPL");

static int tssd_major = 0;
module_param(tssd_major, int, 0);
static int hardsect_size = 512;
module_param(hardsect_size, int, 0);
static int nsectors = 1024 * 4;	/* How big the drive is */
module_param(nsectors, int, 0);
static int ndevices = 1;

/*
 * Minor number and partition management.
 */
#define TSSD_MINORS	16
#define MINOR_SHIFT	4
#define DEVNUM(kdevnum)	(MINOR(kdev_t_to_nr(kdevnum)) >> MINOR_SHIFT

/*
 * We can tweak our hardware sector size, but the kernel talks to us
 * in terms of small sectors, always.
 */
#define KERNEL_SECTOR_SIZE	512

/*
 * After this much idle time, the driver will simulate a media change.
 */
#define INVALIDATE_DELAY	30*HZ

#define DEFAULT_SESSION_KEY 	1234

/*
 * The internal representation of our device.
 */
struct tssd_dev {
        int size;                       /* Device size in sectors */
        u8 *data;                       /* The data array */
	unsigned short *sector_uids;
	unsigned short *sessions;
        short users;                    /* How many users */
        short media_change;             /* Flag a media change? */
        spinlock_t lock;                /* For mutual exclusion */
        struct request_queue *queue;    /* The device request queue */
        struct gendisk *gd;             /* The gendisk structure */
        struct timer_list timer;        /* For simulated media changes */
};

#define TSSD_DEV(blk_dev) (blk_dev->bd_disk->private_data)

static struct tssd_dev *Devices = NULL;

#define tssd_for_each_sector(sector, pos, start, end)	\
	for(pos = start, sector = start / hardsect_size;	\
		pos < end; pos += hardsect_size, 		\
		sector = pos / hardsect_size)	

/*
 * Handle an I/O request.
 */
static int tssd_transfer(struct tssd_dev *dev, unsigned long offset,
		unsigned long nbytes, char *buffer, int write, 
		unsigned long session_key)
{
	unsigned short session_uid;
	unsigned long start, end, pos;
	int sector;
	int invalid_access = 0;

	if ((offset + nbytes) > dev->size) {
		return 1;
	}
	
	session_uid = dev->sessions[session_key & 1];
	start = offset; end = start + nbytes;
	if (write) {
		// assign new uids
		tssd_for_each_sector(sector, pos, start, end) {
			dev->sector_uids[sector] = session_uid;
		}
		memcpy(dev->data + offset, buffer, nbytes);
	}
	else {
		// check uids
		tssd_for_each_sector(sector, pos, start, end) {
			if(dev->sector_uids[sector] != session_uid) {
				invalid_access = 1;
				break;
			}
		}
		if(invalid_access)
			memset(buffer, 0, nbytes);
		else
			memcpy(buffer, dev->data + offset, nbytes);
	}
	return 0;
}

/*
 * The simple form of the request function.
 */
/*
 * Transfer a single BIO.
 */
static int tssd_xfer_bio(struct tssd_dev *dev, struct bio *bio)
{
	int i;
	struct bio_vec *bvec;
  	unsigned long offset = bio->bi_sector * hardsect_size;
  	char *buffer;
	int res = 0;

	spin_lock(&dev->lock);
	/* Do each segment independently. */
	bio_for_each_segment(bvec, bio, i) {
    		bio->bi_idx = i;
		buffer = __bio_kmap_atomic(bio, i, KM_USER0);
		res = tssd_transfer(dev, offset, bio_cur_bytes(bio),
				buffer, bio_data_dir(bio) == WRITE,
				bio->bi_session_key);
		__bio_kunmap_atomic(buffer, KM_USER0);
		if (res) goto out;
		offset += bio_cur_bytes(bio);
	}
out:
	spin_unlock(&dev->lock);
	return res;
}

/*
 * The direct make request version.
 */
static void tssd_make_request(struct request_queue *q, struct bio *bio)
{
	struct tssd_dev *dev = q->queuedata;
	int status;

	PDEBUG("> tssd_make_request: session_key = %lu", bio->bi_session_key);
	status = tssd_xfer_bio(dev, bio);
	bio_endio(bio, status);
	PDEBUG("< tssd_make_request");
}

/*
 * Open and close.
 */
static int tssd_open(struct block_device *blk_dev, fmode_t mod)
{
	struct tssd_dev *dev = TSSD_DEV(blk_dev);

	del_timer_sync(&dev->timer);
	spin_lock(&dev->lock);
	if (! dev->users) 
		check_disk_change(blk_dev);
	dev->users++;
	spin_unlock(&dev->lock);
	PDEBUG("device openned succesfully");
	return 0;
}

static int tssd_release(struct gendisk* gd, fmode_t mod)
{
	struct tssd_dev *dev = gd->private_data;

	spin_lock(&dev->lock);
	dev->users--;

	if (!dev->users) {
		dev->timer.expires = jiffies + INVALIDATE_DELAY;
		add_timer(&dev->timer);
	}
	spin_unlock(&dev->lock);
	PDEBUG("device release successfully");
	return 0;
}

/*
 * Look for a (simulated) media change.
 */
int tssd_media_changed(struct gendisk *gd)
{
	struct tssd_dev *dev = gd->private_data;
	
	return dev->media_change;
}

/*
 * Revalidate.  WE DO NOT TAKE THE LOCK HERE, for fear of deadlocking
 * with open.  That needs to be reevaluated.
 */
int tssd_revalidate(struct gendisk *gd)
{
	struct tssd_dev *dev = gd->private_data;
	
	if (dev->media_change) {
		dev->media_change = 0;
		memset (dev->data, 0, dev->size);
	}
	return 0;
}

/*
 * The "invalidate" function runs out of the device timer; it sets
 * a flag to simulate the removal of the media.
 */
void tssd_invalidate(unsigned long ldev)
{
	struct tssd_dev *dev = (struct tssd_dev *) ldev;

	spin_lock(&dev->lock);
	if (dev->users || !dev->data) 
		PDEBUG ("tssd: timer sanity check failed");
	else
		dev->media_change = 1;
	spin_unlock(&dev->lock);
}

/*
 * The ioctl() implementation
 */

int tssd_ioctl (struct block_device * blk_dev, fmode_t mod,
                 unsigned int cmd, unsigned long arg)
{
	long size;
	struct hd_geometry geo;
	struct tssd_dev *dev = TSSD_DEV(blk_dev);

	switch(cmd) {
  case HDIO_GETGEO:
      /*
		 * Get geometry: since we are a virtual device, we have to make
		 * up something plausible.  So we claim 16 sectors, four heads,
		 * and calculate the corresponding number of cylinders.  We set the
		 * start of data at sector four.
		 */
		size = dev->size*(hardsect_size/KERNEL_SECTOR_SIZE);
		geo.cylinders = (size & ~0x3f) >> 6;
		geo.heads = 4;
		geo.sectors = 16;
		geo.start = 4;
		if (copy_to_user((void __user *) arg, &geo, sizeof(geo)))
			return -EFAULT;
		return 0;
	}

	return -ENOTTY; /* unknown command */
}



/*
 * The device operations structure.
 */
static struct block_device_operations tssd_ops = {
	.owner           = THIS_MODULE,
	.open 	         = tssd_open,
	.release 	       = tssd_release,
	.media_changed   = tssd_media_changed,
	.revalidate_disk = tssd_revalidate,
	.ioctl	         = tssd_ioctl
};


/*
 * Set up our internal device.
 */
static void setup_device(struct tssd_dev *dev, int which)
{
	/*
	 * Get some memory.
	 */
	memset (dev, 0, sizeof (struct tssd_dev));
	dev->size = nsectors*hardsect_size;
	dev->data = kmalloc(dev->size, GFP_KERNEL);
	if (!dev->data) {
		printk (KERN_NOTICE "kmalloc for dev->data failed.\n");
		return;
	}
	memset(dev->data, 0, dev->size);
	dev->sector_uids = kmalloc(sizeof(unsigned short) * nsectors, GFP_KERNEL);
	if (!dev->sector_uids) {
		printk (KERN_NOTICE "kmalloc for dev->sector_uids failed.\n");
		return;
	}
	memset(dev->sector_uids, 0, sizeof(unsigned short) * nsectors);
	dev->sessions = kmalloc(sizeof(unsigned short) * 2, GFP_KERNEL);
	if (!dev->sessions){
		printk (KERN_NOTICE "kmalloc for dev->sessions failed.\n");
		return;
	}
	dev->sessions[0] = 0;
	dev->sessions[1] = 1;
	
	spin_lock_init(&dev->lock);
	
	/*
	 * The timer which "invalidates" the device.
	 */
	init_timer(&dev->timer);
	dev->timer.data = (unsigned long) dev;
	dev->timer.function = tssd_invalidate;
	
 	dev->queue = blk_alloc_queue(GFP_KERNEL);
	if (dev->queue == NULL)
		goto out_vfree;
	blk_queue_make_request(dev->queue, tssd_make_request);
	/* This function is no longer available in Linux 2.6.32.
	 * A possible replacement is blk_queue_physical_block_size()
   	 * blk_queue_hardsect_size(dev->queue, hardsect_size); */
	dev->queue->queuedata = dev;
	/*
	 * And the gendisk structure.
	 */
	dev->gd = alloc_disk(TSSD_MINORS);
	if (! dev->gd) {
		printk (KERN_NOTICE "alloc_disk failure\n");
		goto out_vfree;
	}
	dev->gd->major = tssd_major;
	dev->gd->first_minor = which*TSSD_MINORS;
	dev->gd->fops = &tssd_ops;
	dev->gd->queue = dev->queue;
	dev->gd->private_data = dev;
	snprintf (dev->gd->disk_name, DISK_NAME_LEN, "tssd%c", which + 'a');
	set_capacity(dev->gd, nsectors*(hardsect_size/KERNEL_SECTOR_SIZE));
	add_disk(dev->gd);
	return;

  out_vfree:
	if (dev->data)
		vfree(dev->data);
}



static int __init tssd_init(void)
{
	int i;

#ifndef CONFIG_FS_TSSD
	printk(KERN_WARNING "tssd: TrustedSSD is not enabled");
	return -EIO;
#endif

	/*
	 * Get registered.
	 */
	tssd_major = register_blkdev(tssd_major, "tssd");
	if (tssd_major <= 0) {
		printk(KERN_WARNING "tssd: unable to get major number\n");
		return -EBUSY;
	}
	/*
	 * Allocate the device array, and initialize each one.
	 */
	Devices = kmalloc(ndevices*sizeof (struct tssd_dev), GFP_KERNEL);
	if (Devices == NULL)
		goto out_unregister;
	for (i = 0; i < ndevices; i++) 
		setup_device(Devices + i, i);
    
	return 0;

  out_unregister:
	unregister_blkdev(tssd_major, "sbd");
	return -ENOMEM;
}

static void tssd_exit(void)
{
	int i;

	for (i = 0; i < ndevices; i++) {
		struct tssd_dev *dev = Devices + i;

		del_timer_sync(&dev->timer);
		if (dev->gd) {
			del_gendisk(dev->gd);
			put_disk(dev->gd);
		}
		if (dev->queue) {
			blk_put_queue(dev->queue);
		}
		if (dev->data)
			vfree(dev->data);
	}
	unregister_blkdev(tssd_major, "tssd");
	kfree(Devices);
}
	
module_init(tssd_init);
module_exit(tssd_exit);

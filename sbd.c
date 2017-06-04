/*
 * A sample, extra-simple block driver. Updated for kernel 2.6.31.
 *
 * (C) 2003 Eklektix, Inc.
 * (C) 2010 Pat Patterson <pat at superpat dot com>
 * Redistributable under the terms of the GNU GPL.
 */ 
 
#include <linux/module.h> 
#include <linux/moduleparam.h> 
#include <linux/init.h> 
 
#include <linux/kernel.h> /* printk() */ 
#include <linux/fs.h>     /* everything... */ 
#include <linux/errno.h>  /* error codes */ 
#include <linux/types.h>  /* size_t */ 
#include <linux/vmalloc.h> 
#include <linux/genhd.h> 
#include <linux/blkdev.h> 
#include <linux/hdreg.h> 
#include <linux/crypto.h> 
 
MODULE_LICENSE("Dual BSD/GPL");  
//static char *Version = "1.4";  
 
//Setting up the size of the block/sector 
static int major_num = 0; 
module_param(major_num, int, 0); 
static int logical_block_size = 512; 
module_param(logical_block_size, int, 0); 
static int nsectors = 1024; /* How big the drive is */ 
module_param(nsectors, int, 0); 

//Setting up the cipher API 
struct crypto_cipher *crypCiph; 
static char *Key = "12345678abcdeftg"; 
module_param(Key, charp, 0644); 
static int keyLength = 16; 
module_param(keyLength, int, 0644); 


/*
 * We can tweak our hardware sector size, but the kernel talks to us 
 * in terms of small sectors, always. 
 */
#define KERNEL_SECTOR_SIZE 512 

/*
 * Our request queue. 
 */
static struct request_queue *Queue; 

/*
 * The internal representation of our device. 
 */
static struct sbd_device { 
	unsigned long size; 
	spinlock_t lock; 
	u8 *data; 
	struct gendisk *gd; 
} Device; 
 
/* 
 * Handle an I/O request. 
 */ 
	//Figure out if read or write. 
	//Read decrypt data (block) 
	//Write encrypt data (block) 
static void sbd_transfer(struct sbd_device *dev, sector_t sector, unsigned long nsect, char *buffer, int write) {
	unsigned long offset = sector * logical_block_size; 
	unsigned long nbytes = nsect * logical_block_size; 
	int i; 

	u8 *encryptedData;
	encryptedData = dev->data + offset;
 
	//Writting 
	if ( write ) { 
 
		//Print Encrypting 
		printk( KERN_NOTICE "Writting - Encrypting Starting\n" ); 	
	 
		//Print unencrypted 
		printk( KERN_NOTICE "Writting - unencrypted:\n" ); 
		for ( i = 0; i < nbytes; i++ ) { 
			printk( "%x", buffer[i] ); 
		} 
		printk( "\n" ); 
			
		//For the amount of bytes 
		for( i = 0; i <  nbytes; i += crypto_cipher_blocksize( crypCiph ) ){ 
			//Encrypt 
			crypto_cipher_encrypt_one( 
				crypCiph,	 		//Cipher 
				dev->data + offset + i,		//Original data 
				buffer + i			//New data
			); 
 
		} 
	 
		//Print encrypted data 
		printk( KERN_NOTICE "Writting - encrypted:\n" ); 
		for ( i = 0; i < nbytes; i++ ) { 
			printk("%x", ( unsigned ) *encryptedData++ );
		}

		//Print End
		printk( KERN_NOTICE "\nWritting - Encrypting Ended\n" );	


	//Reading
	} else {

		//Print Decrypting	
		printk( KERN_NOTICE "Reading - Decrypting Starting\n" );	

		//Print Encrypted 
		printk( KERN_NOTICE "Reading - encrypted:\n" ); 
		for ( i = 0; i < nbytes; i++ ) { 
			printk( "%x", buffer[i] ); 
		} 
		printk( "\n" ); 
 
		//For the amount of bytes
		for( i = 0; i <  nbytes; i += crypto_cipher_blocksize( crypCiph ) ){
 
			//Decrypt
			crypto_cipher_decrypt_one(
				crypCiph,		 	//Cipher
				buffer + i,			//Original data
				dev->data + offset + i		//New data
			);

		}
	
		//Print Unencrypted data
		printk( KERN_NOTICE "Reading - unencrypted:\n" );
		for (i = 0; i < nbytes; i++) {
			printk("%x", ( unsigned ) *encryptedData++ );
		}

		//Print End
		printk( KERN_NOTICE "\nReading - Decrypting Ended\n" );	
		
	}

}

static void sbd_request(struct request_queue *q) { 
	struct request *req; 
 
	req = blk_fetch_request(q); 
	while (req != NULL) { 
		// blk_fs_request() was removed in 2.6.36 - many thanks to 
		// Christian Paro for the heads up and fix... 
		//if (!blk_fs_request(req)) { 
		if (req == NULL || (req->cmd_type != REQ_TYPE_FS)) { 
			printk (KERN_NOTICE "Skip non-CMD request\n"); 
			__blk_end_request_all(req, -EIO); 
			continue; 
		}
		sbd_transfer(&Device, blk_rq_pos(req), blk_rq_cur_sectors(req), req->buffer, rq_data_dir(req));
		if ( ! __blk_end_request_cur(req, 0) ) { 
			req = blk_fetch_request(q); 
		}
	}
}

/*
 * The HDIO_GETGEO ioctl is handled in blkdev_ioctl(), which
 * calls this. We need to implement getgeo, since we can't
 * use tools such as fdisk to partition the drive otherwise.
 */
int sbd_getgeo(struct block_device * block_device, struct hd_geometry * geo) {
	long size;

	/* We have no real geometry, of course, so make something up. */
	size = Device.size * (logical_block_size / KERNEL_SECTOR_SIZE); 
	geo->cylinders = (size & ~0x3f) >> 6; 
	geo->heads = 4; 
	geo->sectors = 16; 
	geo->start = 0; 
	return 0; 
}

/*
 * The device operations structure.
 */
static struct block_device_operations sbd_ops = {
		.owner  = THIS_MODULE, 
		.getgeo = sbd_getgeo 
};

//Allocate the cipher
static int __init sbd_init(void) {
	
	//Allocate the cipher
	crypCiph = crypto_alloc_cipher( "aes", 0, 0 );

	/*
	 * Set up our internal device.
	 */
	Device.size = nsectors * logical_block_size;
	spin_lock_init(&Device.lock);
	Device.data = vmalloc(Device.size);
		

	if (Device.data == NULL) 
		return -ENOMEM; 
	/*
	 * Get a request queue.
	 */
	Queue = blk_init_queue(sbd_request, &Device.lock); 
	if (Queue == NULL) 
		goto out;
	blk_queue_logical_block_size(Queue, logical_block_size); 
	/*
	 * Get registered.
	 */
	major_num = register_blkdev(major_num, "sbd");
	if (major_num < 0) {
		printk(KERN_WARNING "sbd: unable to get major number\n");
		goto out;
	}
	/*
	 * And the gendisk structure. 
	 */
	Device.gd = alloc_disk(16); 
	if (!Device.gd) 
		goto out_unregister; 
	Device.gd->major = major_num; 
	Device.gd->first_minor = 0; 
	Device.gd->fops = &sbd_ops; 
	Device.gd->private_data = &Device; 
	strcpy(Device.gd->disk_name, "sbd0"); 
	set_capacity(Device.gd, nsectors); 
	Device.gd->queue = Queue; 
	add_disk(Device.gd); 

	return 0;

out_unregister:
	unregister_blkdev(major_num, "sbd"); 
out:
	vfree(Device.data); 
	return -ENOMEM; 
}

//Free the cipher
static void __exit sbd_exit(void) { 
	
	//Free the cipher
	crypto_free_cipher( crypCiph ); 

	del_gendisk(Device.gd); 
	put_disk(Device.gd); 
	unregister_blkdev(major_num, "sbd" ); 
	blk_cleanup_queue(Queue); 
	vfree(Device.data); 
}
 
module_init(sbd_init); 
module_exit(sbd_exit); 

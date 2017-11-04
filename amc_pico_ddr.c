
#include "amc_pico_char.h"

static
int char_ddr_open(struct inode *inode, struct file *file)
{
    int ret;
    struct board_data *board = container_of(inode->i_cdev, struct board_data, cdev_ddr);

    dev_dbg(&board->pci_dev->dev, "%s()\n", __FUNCTION__);

    if (!try_module_get(THIS_MODULE))
        return -ENODEV;

    if (!kobject_get(&board->cdev_ddr.kobj)) {
        ret = -ENODEV;
        goto unmod;
    }

    if (!kobject_get(&board->kobj)) {
        ret = -ENODEV;
        goto uncdev;
    }

    file->private_data = board;

    return 0;
//unobj:
//    kobject_put(&board->kobj);
uncdev:
    kobject_put(&board->cdev_ddr.kobj);
unmod:
    module_put(THIS_MODULE);
    return ret;
}

static
int char_ddr_release(struct inode *inode, struct file *filp)
{
    struct board_data *board = (struct board_data *)filp->private_data;

    dev_dbg(&board->pci_dev->dev, "%s()\n", __FUNCTION__);

    kobject_put(&board->kobj);
    kobject_put(&board->cdev_ddr.kobj);
    module_put(THIS_MODULE);
    return 0;
}

static
long char_ddr_ioctl(
    struct file *filp,
    unsigned int cmd,
    unsigned long arg
)
{
    switch(cmd) {
    case GET_VERSION:
        return put_user(GET_VERSION_CURRENT, (uint32_t*)arg);
    default:
        return -ENOTTY;
    }
}

static
loff_t char_ddr_llseek(struct file *filp, loff_t pos, int whence)
{
    struct board_data *board = (struct board_data *)filp->private_data;
    loff_t npos;
    ssize_t limit = resource_size(&board->pci_dev->resource[2])*DDR_SELECT_COUNT;

    switch(whence) {
    case 0: npos = pos; break;
    case 1: npos = filp->f_pos + pos; break;
    case 2: npos = limit + pos; break;
    default: return -EINVAL;
    }

    if(npos<0)
        return -EINVAL;
    else if(npos>limit)
        npos = limit;

    filp->f_pos = npos;
    return npos;
}

static
ssize_t char_ddr_write(struct file *filp, const char __user *buf, size_t count, loff_t *pos)
{
    ssize_t ret=0;
    struct board_data *board = (struct board_data *)filp->private_data;
    size_t page_size = resource_size(&board->pci_dev->resource[2]),
           limit = page_size*DDR_SELECT_COUNT,
           remaining;
    loff_t npos = 0;

    if(pos) npos = *pos;

    dev_dbg(&board->pci_dev->dev, "DDR write(%lu, %zu) (page_size=%zu)\n", (unsigned long)npos, count, page_size);

    /* round down to word boundary */
    count &= ~3;
    npos  &= ~3;

    if(npos>limit) npos = limit;

    if(count>limit-npos) count = limit-npos;

    if(!count) return count;

    remaining = count;

    if(mutex_lock_interruptible(&board->ddr_lock))
        return -EINTR;

    while(ret==0 && remaining>0) {

        unsigned page = npos/page_size;
        uint32_t poffset = npos%page_size,
                 plimit = poffset + remaining;

        if(unlikely(page>=DDR_SELECT_COUNT)) {
            ret = -EINVAL;
            WARN(1, "Page selection logic error %lu %zu\n", (unsigned long)npos, page_size);
            break;
        }

        if(plimit>page_size)
            plimit = page_size;

        remaining -= plimit-poffset;
        npos      += plimit-poffset;

        /* page select */
        iowrite32(page, board->bar0+DDR_SELECT);

        for(; !ret && poffset<plimit; poffset+=4, buf+=4) {
            uint32_t val;
            ret = get_user(val, (uint32_t*)buf);
            if(!ret)
                iowrite32(val, board->bar2+poffset);
        }
    }

    mutex_unlock(&board->ddr_lock);

    if(ret) {
        dev_dbg(&board->pci_dev->dev, "  ERR %zd\n", ret);
        return ret;

    } else {
        count -= remaining;
        if(pos) *pos = npos;

        dev_dbg(&board->pci_dev->dev, "  POS %lu CNT %zd\n", (unsigned long)npos, count);
        return count;
    }
}

static
ssize_t char_ddr_read(
        struct file *filp,
        char __user *buf,
        size_t count,
        loff_t *pos
        )
{
    ssize_t ret=0;
    struct board_data *board = (struct board_data *)filp->private_data;
    size_t page_size = resource_size(&board->pci_dev->resource[2]),
           limit = page_size*DDR_SELECT_COUNT;
    loff_t npos = 0;

    if(pos) npos = *pos;

    dev_dbg(&board->pci_dev->dev, "DDR read(%lu, %zu) (page_size=%zu)\n", (unsigned long)npos, count, page_size);

    /* round down to word boundary */
    count &= ~3u;
    npos  &= ~3u;

    if(npos>limit) npos = limit;

    if(count>limit-npos) count = limit-npos;

    if(!count) return count;

    limit = npos + count;
    /* Need to read and store [npos, limit) */

    count = 0;

    /* prevent concurrent access to DDR_SELECT */
    if(mutex_lock_interruptible(&board->ddr_lock))
        return -EINTR;

    while(npos<limit && !ret) {
        /* page and offset in device */
        unsigned page = npos/page_size, i,
                 final_page = (limit-1)/page_size;

        uint32_t devoffset = npos%page_size,
                 devlimit = page==final_page ? (limit%page_size) : page_size;

        if(signal_pending(current)) {
            ret = -ERESTARTSYS;
            break;
        }
        /* relinquish CPU occasionally */
        schedule();

        dev_dbg(&board->pci_dev->dev,"READ Page %u [%u, %u)\n",
                page, (unsigned)devoffset, (unsigned)devlimit);

        iowrite32(page, board->bar0+DDR_SELECT);

        for(i=devoffset; i<devlimit && !ret; i+=4, buf+=4) {
            uint32_t val = ioread32(board->bar2+i);

            ret = put_user(val, (uint32_t*)buf);
        }

        npos  += devlimit-devoffset;
        count += devlimit-devoffset;
    }

    mutex_unlock(&board->ddr_lock);

    if(ret) {
        dev_dbg(&board->pci_dev->dev, "  ERR %zd\n", ret);
        return ret;

    } else {
        if(pos) *pos = npos;

        dev_dbg(&board->pci_dev->dev, "  POS %lu CNT %zd\n", (unsigned long)npos, count);
        return count;
    }
}

const struct file_operations amc_ddr_fops = {
    .owner		= THIS_MODULE,
    .open		= char_ddr_open,
    .release	= char_ddr_release,
    .unlocked_ioctl = char_ddr_ioctl,
    .read		= char_ddr_read,
    .write      = char_ddr_write,
    .llseek     = char_ddr_llseek,
};


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
    struct board_data *board = (struct board_data *)filp->private_data;
    size_t page_size = resource_size(&board->pci_dev->resource[2]),
           limit = page_size*DDR_SELECT_COUNT;
    unsigned page;
    uint32_t poffset, plimit;
    loff_t npos = 0;

    if(pos) npos = *pos;

    /* round down to word boundary */
    count &= ~3;
    npos  &= ~3;

    if(npos>limit) npos = limit;

    if(count>limit-npos) count = limit-npos;
    if(!count) return count;

    page = npos/page_size;
    poffset = npos%page_size;
    plimit = poffset + count;

    WARN(page>=DDR_SELECT_COUNT, "Page select validation error %u\n", page);
    if(page>=DDR_SELECT_COUNT) return -EINVAL;

    /* page select */
    iowrite32(page, board->bar0+DDR_SELECT);

    for(; poffset<plimit; poffset+=4, buf+=4) {
        uint32_t val;
        int ret = get_user(val, (uint32_t*)buf);
        if(ret) return ret;
        iowrite32(val, board->bar2+poffset);
    }

    if(*pos) *pos = poffset;

    return count;
}

static
ssize_t char_ddr_read(
        struct file *filp,
        char __user *buf,
        size_t count,
        loff_t *pos
        )
{
    ssize_t ret;
    struct board_data *board = (struct board_data *)filp->private_data;
    size_t page_size = resource_size(&board->pci_dev->resource[2]),
           limit = page_size*DDR_SELECT_COUNT;
    unsigned page;
    uint32_t poffset, plimit;
    loff_t npos = 0;

    if(pos) npos = *pos;

    /* round down to word boundary */
    count &= ~3u;
    npos  &= ~3u;

    if(npos>limit) npos = limit;

    if(count>limit-npos) count = limit-npos;
    if(!count) return count;

    page = npos/page_size;
    poffset = npos%page_size;
    plimit = poffset + count;

    WARN(page>=DDR_SELECT_COUNT, "Page select validation error %u\n", page);
    if(page>=DDR_SELECT_COUNT) return -EINVAL;

    /* page select */
    iowrite32(page, board->bar0+DDR_SELECT);

    for(ret=0; !ret && poffset<plimit; poffset+=4, buf+=4) {
        uint32_t val = ioread32(board->bar2+poffset);
        ret = put_user(val, (uint32_t*)buf);
    }

    if(!ret) {
        ret = count;
        if(*pos) *pos = poffset;
    }

    return count;
}

const struct file_operations amc_ddr_fops = {
    .owner		= THIS_MODULE,
    .open		= char_ddr_open,
    .release	= char_ddr_release,
    .read		= char_ddr_read,
    .write      = char_ddr_write,
    .llseek     = char_ddr_llseek,
};

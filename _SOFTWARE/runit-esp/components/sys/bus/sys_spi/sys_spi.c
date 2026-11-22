/**
 * @file
 * @brief Klient kontraktu SPI (sys_spi): każde wywołanie to operacja magistrali układu; nic tu nie
 *        wie, jak magistrala jest zrobiona. API i błędy: sys_spi.h.
 */
#include "sys_spi/sys_spi.h"

ERR_MODULE_DEFINE(MODULE_ID_SPI, ERR_SYS_SPI_MAP)

static err_h check_dev(const sys_spi_dev_t *dev)
{
    ERR_CHECK(dev != NULL, ERR_BAD_ARG);
    ERR_CHECK(dev->attached, ERR_SPI_DEV_NOT_ADDED, .bus = dev->bus, .cs = dev->cs);
    return NULL;
}

static err_h xfer(const sys_spi_dev_t *dev, uint8_t flags, const uint8_t *tx, size_t tx_len,
                  uint8_t *rx, size_t rx_len)
{
    const spi_xfer_t x = {
        .cs = dev->cs, .flags = flags, .tx = tx, .tx_len = tx_len, .rx = rx, .rx_len = rx_len};
    return SYS_DEVICE_WRITE(dev->bus, CONTRACT_SPI, OP_XFER, &x);
}

/* Zasób właściciela: DETACH podłączonego; potem niepodłączony, także po błędzie magistrali. */
static err_h dev_release(void *arg)
{
    sys_spi_dev_t *dev = arg;
    if (!dev->attached) {
        return NULL;
    }
    dev->attached = 0;
    return SYS_DEVICE_SET(dev->bus, CONTRACT_SPI, OP_DETACH, .cs = dev->cs);
}

err_h sys_spi_dev_attach(uint8_t owner, sys_spi_dev_t *dev, uint8_t bus,
                         const spi_attach_args_t *args)
{
    ERR_CHECK(dev != NULL && args != NULL, ERR_BAD_ARG);
    dev->bus = bus;
    dev->cs = args->cs;
    ERR_TRY(sys_device_require_op(owner, bus, CONTRACT_SPI_OP_XFER, CMD_DIR_WRITE));
    ERR_TRY(sys_device_res_add(owner, &dev->res, dev_release, dev)); /* przed zajęciem cs */
    ERR_TRY(SYS_DEVICE_WRITE(bus, CONTRACT_SPI, OP_ATTACH, args));
    dev->attached = 1;
    return NULL;
}

err_h sys_spi_write(const sys_spi_dev_t *dev, const uint8_t *data, size_t len)
{
    ERR_TRY(check_dev(dev));
    ERR_CHECK(data != NULL && len > 0, ERR_BAD_ARG);
    return xfer(dev, 0, data, len, NULL, 0);
}

err_h sys_spi_read(const sys_spi_dev_t *dev, uint8_t *data, size_t len)
{
    ERR_TRY(check_dev(dev));
    ERR_CHECK(data != NULL && len > 0, ERR_BAD_ARG);
    return xfer(dev, 0, NULL, 0, data, len);
}

err_h sys_spi_write_read(const sys_spi_dev_t *dev, const uint8_t *wdata, size_t wlen,
                         uint8_t *rdata, size_t rlen)
{
    ERR_TRY(check_dev(dev));
    ERR_CHECK(wdata != NULL && wlen > 0 && rdata != NULL && rlen > 0, ERR_BAD_ARG);
    return xfer(dev, 0, wdata, wlen, rdata, rlen);
}

err_h sys_spi_transfer(const sys_spi_dev_t *dev, const uint8_t *tx, uint8_t *rx, size_t len)
{
    ERR_TRY(check_dev(dev));
    ERR_CHECK(tx != NULL && rx != NULL && len > 0, ERR_BAD_ARG);
    return xfer(dev, SYS_SPI_XFER_DUPLEX, tx, len, rx, len);
}

err_h sys_spi_queue(const sys_spi_dev_t *dev, const spi_xfer_t *xfer_in)
{
    ERR_TRY(check_dev(dev));
    ERR_CHECK(xfer_in != NULL, ERR_BAD_ARG);
    ERR_CHECK(xfer_in->tx != NULL || xfer_in->rx != NULL, ERR_BAD_ARG);
    ERR_CHECK((xfer_in->flags & ~SYS_SPI_XFER_DUPLEX) == 0, ERR_BAD_ARG);
    spi_xfer_t q = *xfer_in;
    q.cs = dev->cs; /* pole wołającego jest ignorowane */
    return SYS_DEVICE_WRITE(dev->bus, CONTRACT_SPI, OP_QUEUE, &q);
}

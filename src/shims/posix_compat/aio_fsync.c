static int __cccc_native_aio_fsync(int op, struct aiocb *aiocbp) {
    if (!aiocbp) { errno = EINVAL; return -1; }
    return aio_fsync(op, aiocbp);
}

#include <sys/mount.h>

#include <magisk.hpp>
#include <base.hpp>
#include <socket.hpp>
#include <sys/vfs.h>

#include "init.hpp"

using namespace std;

// 一阶段的准备工作
// 1. 拷贝 ramdisk 中与magisk相关的文件到 /data tmpfs 中
// 2. 寻找原生 init 可执行程序并恢复, 准备执行原生 init
// 3. 对原生 init 可执行程序写入补丁, 替换字符串
void FirstStageInit::prepare() {
    prepare_data();
    restore_ramdisk_init();
    // 使用 mmap 加载原生init可执行程序到内存中, 并生成 mmap_data 类的对象
    auto init = mmap_data("/init", true);

    // 搜索内存, 将 `/system/bin/init` 字符串替换为 `/data/magiskinit`
    // 这里的内存是原生init可执行程序, 需要注意的是, 替换前后的两个字符串长度相同
    // 因为是通过 mmap fd 的形式加载到内存, 因此对内存的更改可以直接反应到文件本身.
    // Redirect original init to magiskinit
    init.patch({ make_pair(INIT_PATH, REDIR_PATH) });

    // 因为 init 是 mmap_data 类的局部对象, 因此离开作用域后会自动析构.
    // mmap_data 的析构函数中会调用 munmap 释放内存
}

void LegacySARInit::first_stage_prep() {
    // Patch init binary
    int src = xopen("/init", O_RDONLY);
    int dest = xopen("/data/init", O_CREAT | O_WRONLY, 0);
    {
        auto init = mmap_data("/init");
        init.patch({ make_pair(INIT_PATH, REDIR_PATH) });
        write(dest, init.buf, init.sz);
        fclone_attr(src, dest);
        close(dest);
        close(src);
    }
    xmount("/data/init", "/init", nullptr, MS_BIND, nullptr);
}

bool SecondStageInit::prepare() {
    umount2("/init", MNT_DETACH);
    unlink("/data/init");

    // Make sure init dmesg logs won't get messed up
    argv[0] = (char *) INIT_PATH;

    // Some weird devices like meizu, uses 2SI but still have legacy rootfs
    if (struct statfs sfs{}; statfs("/", &sfs) == 0 && sfs.f_type == RAMFS_MAGIC) {
        // We are still on rootfs, so make sure we will execute the init of the 2nd stage
        unlink("/init");
        xsymlink(INIT_PATH, "/init");
        return true;
    }
    return false;
}

#include <sys/stat.h>
#include <sys/types.h>
#include <libgen.h>
#include <vector>

#include <xz.h>

#include <base.hpp>
#include <embed.hpp>

#include "init.hpp"

#include <init-rs.cpp>

using namespace std;

bool unxz(int fd, const uint8_t *buf, size_t size) {
    uint8_t out[8192];
    xz_crc32_init();
    struct xz_dec *dec = xz_dec_init(XZ_DYNALLOC, 1 << 26);
    struct xz_buf b = {
        .in = buf,
        .in_pos = 0,
        .in_size = size,
        .out = out,
        .out_pos = 0,
        .out_size = sizeof(out)
    };
    enum xz_ret ret;
    do {
        ret = xz_dec_run(dec, &b);
        if (ret != XZ_OK && ret != XZ_STREAM_END)
            return false;
        write(fd, out, b.out_pos);
        b.out_pos = 0;
    } while (b.in_pos != size);
    return true;
}

static int dump_bin(const uint8_t *buf, size_t sz, const char *path, mode_t mode) {
    int fd = xopen(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0)
        return 1;
    if (!unxz(fd, buf, sz))
        return 1;
    close(fd);
    return 0;
}

// 从ramdisk中备份的原生init恢复原生init
void restore_ramdisk_init() {
    unlink("/init");

    // 获取原生init的路径
    const char *orig_init = backup_init();
    if (access(orig_init, F_OK) == 0) {
        // 移动 原生init 到 /init
        xrename(orig_init, "/init");
    } else {
        // 如果缺少备份的init，这意味着 boot ramdisk是从头开始创建的，
        // 而真正的init在一个单独的CPIO中，保证放在/system/bin/init
        // 该流程忽略
        // If the backup init is missing, this means that the boot ramdisk
        // was created from scratch, and the real init is in a separate CPIO,
        // which is guaranteed to be placed at /system/bin/init.
        xsymlink(INIT_PATH, "/init");
    }
}

int dump_preload(const char *path, mode_t mode) {
    return dump_bin(init_ld_xz, sizeof(init_ld_xz), path, mode);
}

class RecoveryInit : public BaseInit {
public:
    using BaseInit::BaseInit;

    void start() override {
        LOGD("Ramdisk is recovery, abort\n");
        restore_ramdisk_init();
        rm_rf("/.backup");
        exec_init();
    }
};

int main(int argc, char *argv[]) {
    umask(0);

    auto name = basename(argv[0]);
    if (name == "magisk"sv)
        return magisk_proxy_main(argc, argv);

    if (getpid() != 1)
        return 1;

    BaseInit *init;
    BootConfig config{};

    if (argc > 1 && argv[1] == "selinux_setup"sv) {
        init = new SecondStageInit(argv);
    } else {
        // This will also mount /sys and /proc
        load_kernel_info(&config);

        file_readline("/proc/self/mountinfo", [&](string_view s) {
            LOGD("mountinfo %s", s.data());
            return true;
        });

        if (config.skip_initramfs) {
            LOGD("config.skip_initramfs");
            init = new LegacySARInit(argv, &config);
        } else if (config.force_normal_boot) {
            LOGD("config.force_normal_boot");
            init = new FirstStageInit(argv, &config);
        } else if (access("/sbin/recovery", F_OK) == 0 || access("/system/bin/recovery", F_OK) == 0) {
            LOGD("/sbin/recovery or /system/bin/recovery exists!");
            init = new RecoveryInit(argv, &config);
        } else if (check_two_stage()) {
            LOGD("check_two_stage");
            init = new FirstStageInit(argv, &config);
        } else {
            LOGD("else!!");
            init = new RootFSInit(argv, &config);
        }
    }

    // Run the main routine
    init->start();
    exit(1);
}

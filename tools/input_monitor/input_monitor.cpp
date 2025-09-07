#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <linux/input.h>
#include <sys/inotify.h>
#include <sys/poll.h>
#include <errno.h>
#include <map>
#include <string>
#include <vector>

struct InputDevice {
    int fd;
    std::string path;
    std::string name;
};

static const char *input_dir = "/dev/input";
static std::map<int, InputDevice> devices;

static void open_device(const char *name) {
    if (strncmp(name, "event", 5)) return;
    std::string path = std::string(input_dir) + "/" + name;
    int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) return;

    char devname[256] = {0};
    ioctl(fd, EVIOCGNAME(sizeof(devname)), devname);

    // Try to grab device (exclusive)
    if (ioctl(fd, EVIOCGRAB, 1) == 0) {
        fprintf(stdout, "Grabbed %s (%s)\n", path.c_str(), devname);
    } else {
        fprintf(stdout, "EVIOCGRAB failed: %s (%s)\n", strerror(errno), devname);
    }

    devices.emplace(fd, InputDevice{fd, path, devname});
    fprintf(stdout, "Opened %s (%s)\n", path.c_str(), devname);
}

static void scan_devices() {
    DIR *dir = opendir(input_dir);
    if (!dir) return;
    struct dirent *ent;
    while ((ent = readdir(dir))) {
        if (ent->d_name[0] == '.') continue;
        open_device(ent->d_name);
    }
    closedir(dir);
}

static void handle_inotify(int fd) {
    char buf[512];
    ssize_t len = read(fd, buf, sizeof(buf));
    if (len <= 0) return;
    for (char *ptr = buf; ptr < buf + len;) {
        struct inotify_event *ev = (struct inotify_event *)ptr;
        if (ev->mask & IN_CREATE) {
            open_device(ev->name);
        }
        if (ev->mask & IN_DELETE) {
            for (auto it = devices.begin(); it != devices.end();) {
                if (it->second.path.substr(strlen(input_dir) + 1) == ev->name) {
                    fprintf(stdout, "Closed %s (%s)\n",
                            it->second.path.c_str(), it->second.name.c_str());
                    close(it->second.fd);
                    it = devices.erase(it);
                } else {
                    ++it;
                }
            }
        }
        ptr += sizeof(struct inotify_event) + ev->len;
    }
}

static void read_events(int fd) {
    struct input_event ev[64];
    ssize_t rd = read(fd, ev, sizeof(ev));
    if (rd < (ssize_t)sizeof(struct input_event)) return;

    int cnt = rd / sizeof(struct input_event);
    for (int i = 0; i < cnt; i++) {
        fprintf(stdout, "%s: type %u code %u value %d\n",
                devices[fd].name.c_str(),
                ev[i].type, ev[i].code, ev[i].value);
    }
    fflush(stdout);
}

int main() {
    scan_devices();

    int in_fd = inotify_init1(IN_NONBLOCK);
    if (in_fd >= 0) {
        inotify_add_watch(in_fd, input_dir, IN_CREATE | IN_DELETE);
    }

    while (true) {
        std::vector<pollfd> pfds;
        if (in_fd >= 0) pfds.push_back({in_fd, POLLIN, 0});
        for (auto &kv : devices) pfds.push_back({kv.first, POLLIN, 0});

        if (pfds.empty()) {
            usleep(100000);
            continue;
        }

        int ret = poll(pfds.data(), pfds.size(), -1);
        if (ret <= 0) continue;

        size_t index = 0;
        if (in_fd >= 0) {
            if (pfds[index].revents & POLLIN) handle_inotify(in_fd);
            index++;
        }
        for (; index < pfds.size(); index++) {
            if (pfds[index].revents & POLLIN) read_events(pfds[index].fd);
        }
    }
    return 0;
}

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#define CMD_LED_ON 1
#define CMD_LED_OFF 0
#define DEVICE_FILE_NAME_LED "/dev/led-dts-platform"
#define DEVICE_FILE_NAME_INPUT_KEY "/dev/input/event1"

#define IIO_SYSFS_BASE "/sys/bus/iio/devices"
#define IIO_PATH_SIZE 256
#define IIO_VALUE_SIZE 64

/* 滑动平均滤波窗口大小 */
#define FILTER_WINDOW 8

/* 滑动平均滤波器 */
typedef struct {
        float buf[FILTER_WINDOW];
        int idx;
        int count;
        float sum;
} moving_avg_t;

static float ma_filter(moving_avg_t *f, float val) {
        f->sum -= f->buf[f->idx];
        f->buf[f->idx] = val;
        f->sum += val;
        f->idx = (f->idx + 1) % FILTER_WINDOW;
        if (f->count < FILTER_WINDOW)
                f->count++;
        return f->sum / (float)f->count;
}

static int read_text_file(const char *path, char *buf, size_t buf_size) {
        ssize_t count;
        int fd;
        int saved_errno;

        if (!path || !buf || buf_size < 2)
                return -EINVAL;

        fd = open(path, O_RDONLY);
        if (fd < 0)
                return -errno;

        do {
                count = read(fd, buf, buf_size - 1);
        } while (count < 0 && errno == EINTR);

        saved_errno = errno;
        close(fd);

        if (count < 0)
                return -saved_errno;
        if (count == 0)
                return -EIO;

        buf[count] = '\0';
        return 0;
}

static int make_iio_attr_path(char *path, size_t path_size,
                              const char *device_path, const char *attr) {
        int count;

        if (!path || !device_path || !attr)
                return -EINVAL;

        count = snprintf(path, path_size, "%s/%s", device_path, attr);
        if (count < 0 || (size_t)count >= path_size)
                return -ENAMETOOLONG;

        return 0;
}

static int read_iio_int(const char *device_path, const char *attr, int *value) {
        char path[IIO_PATH_SIZE];
        char buf[IIO_VALUE_SIZE];
        char *end;
        long parsed;
        int ret;

        if (!value)
                return -EINVAL;

        ret = make_iio_attr_path(path, sizeof(path), device_path, attr);
        if (ret)
                return ret;

        ret = read_text_file(path, buf, sizeof(buf));
        if (ret)
                return ret;

        errno = 0;
        parsed = strtol(buf, &end, 10);
        if (errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX)
                return -ERANGE;
        if (end == buf)
                return -EINVAL;
        while (*end && isspace((unsigned char)*end))
                end++;
        if (*end)
                return -EINVAL;

        *value = (int)parsed;
        return 0;
}

static int read_iio_float(const char *device_path, const char *attr,
                          float *value) {
        char path[IIO_PATH_SIZE];
        char buf[IIO_VALUE_SIZE];
        char *end;
        float parsed;
        int ret;

        if (!value)
                return -EINVAL;

        ret = make_iio_attr_path(path, sizeof(path), device_path, attr);
        if (ret)
                return ret;

        ret = read_text_file(path, buf, sizeof(buf));
        if (ret)
                return ret;

        errno = 0;
        parsed = strtof(buf, &end);
        if (errno == ERANGE)
                return -ERANGE;
        if (end == buf)
                return -EINVAL;
        while (*end && isspace((unsigned char)*end))
                end++;
        if (*end)
                return -EINVAL;

        *value = parsed;
        return 0;
}

static int find_iio_device(const char *name, char *device_path,
                           size_t device_path_size) {
        struct dirent *entry;
        char candidate[IIO_PATH_SIZE];
        char name_path[IIO_PATH_SIZE];
        char actual_name[IIO_VALUE_SIZE];
        DIR *dir;
        int count;
        int ret = -ENODEV;

        if (!name || !device_path || device_path_size == 0)
                return -EINVAL;

        dir = opendir(IIO_SYSFS_BASE);
        if (!dir)
                return -errno;

        while ((entry = readdir(dir)) != NULL) {
                if (strncmp(entry->d_name, "iio:device",
                            strlen("iio:device")) != 0)
                        continue;

                count = snprintf(candidate, sizeof(candidate), "%s/%s",
                                 IIO_SYSFS_BASE, entry->d_name);
                if (count < 0 || (size_t)count >= sizeof(candidate))
                        continue;

                count = snprintf(name_path, sizeof(name_path), "%s/name",
                                 candidate);
                if (count < 0 || (size_t)count >= sizeof(name_path))
                        continue;

                if (read_text_file(name_path, actual_name,
                                   sizeof(actual_name)) != 0)
                        continue;

                actual_name[strcspn(actual_name, "\r\n")] = '\0';
                if (strcmp(actual_name, name) != 0)
                        continue;

                count = snprintf(device_path, device_path_size, "%s",
                                 candidate);
                if (count < 0 || (size_t)count >= device_path_size)
                        ret = -ENAMETOOLONG;
                else
                        ret = 0;
                break;
        }

        closedir(dir);
        return ret;
}

static void report_iio_error(const char *sensor, const char *attr, int error) {
        fprintf(stderr, "%s: read %s failed: %s\n", sensor, attr,
                strerror(error < 0 ? -error : error));
}

// 任务类型枚举
typedef enum {
        TASK_READ_AP3216C,
        TASK_READ_ICM20608,
        TASK_BLINK_LED
} task_type_t;

typedef void (*task_fn)(void *arg);

// 任务节点
typedef struct task_node {
        task_type_t type;
        task_fn fn;
        void *param; // 预留参数
        struct task_node *next;
} task_node_t;

typedef struct {
        pthread_mutex_t lock; // 互斥锁，保护队列
        pthread_cond_t cond;  // 条件变量，有任务时唤醒线程
        task_node_t *head;    // 任务队列头
        task_node_t *tail;    // 任务队列尾
        int shutdown;         // 销毁标志
        pthread_t *threads;   // 线程数组
        int max_thread_num;
} thread_pool_t;

static void *worker_thread(void *arg) {
        thread_pool_t *pool = arg;
        while (1) {
                pthread_mutex_lock(&pool->lock);
                while (!pool->head && !pool->shutdown) {
                        pthread_cond_wait(&pool->cond, &pool->lock);
                }
                if (pool->shutdown && !pool->head) {
                        pthread_mutex_unlock(&pool->lock);
                        break;
                }

                task_node_t *task = pool->head;
                pool->head = task->next;
                if (!pool->head) {
                        pool->tail = NULL;
                }
                pthread_mutex_unlock(&pool->lock);

                if (task->fn) {
                        task->fn(task->param);
                }
                free(task);
        }
        return NULL;
}

int thread_pool_submit(thread_pool_t *pool, task_type_t type, task_fn fn,
                       void *param) {
        task_node_t *node = malloc(sizeof(*node));
        if (!node)
                return -1;
        node->type = type;
        node->fn = fn;
        node->param = param;
        node->next = NULL;

        pthread_mutex_lock(&pool->lock);
        if (pool->shutdown) {
                pthread_mutex_unlock(&pool->lock);
                free(node);
                return -1;
        }
        if (pool->tail)
                pool->tail->next = node;
        else
                pool->head = node;
        pool->tail = node;
        pthread_cond_signal(&pool->cond);
        pthread_mutex_unlock(&pool->lock);
        return 0;
}

void task_blink_led(void *arg) {
        const char *dev = (const char *)arg;
        if (!dev) {
                fprintf(stderr, "task_blink_led:设备路径为空\r\n");
                return;
        }

        int fd = open(dev, O_RDWR);
        if (fd < 0) {
                perror("task_blink_led:open led fail\r\n");
                return;
        }

        unsigned char on = 1;
        if (write(fd, &on, sizeof(on)) < 0) {
                perror("task_blink_led: led on fail\r\n");
                close(fd);
                return;
        }

        usleep(300 * 1000);
        unsigned char off = 0;
        if (write(fd, &off, sizeof(off)) < 0) {
                perror("task_blink_led:led off fail\r\n");
        }

        close(fd);
}

void task_read_ap3216c(void *arg) {
        const char *device_path = (const char *)arg;
        char ir_text[32];
        char ps_text[32];
        int als_raw = 0;
        int ir_raw = 0;
        int ps_raw = 0;
        float als_scale = 0.0f;
        int ret;
        static pthread_mutex_t sample_lock = PTHREAD_MUTEX_INITIALIZER;

        if (!device_path) {
                fprintf(stderr, "AP3216C: IIO device path is NULL\n");
                return;
        }
        if (pthread_mutex_trylock(&sample_lock) != 0)
                return;

        ret = read_iio_int(device_path, "in_illuminance_raw", &als_raw);
        if (ret) {
                report_iio_error("AP3216C", "in_illuminance_raw", ret);
                goto unlock;
        }

        ret = read_iio_float(device_path, "in_illuminance_scale", &als_scale);
        if (ret) {
                report_iio_error("AP3216C", "in_illuminance_scale", ret);
                goto unlock;
        }

        ret = read_iio_int(device_path, "in_intensity_ir_raw", &ir_raw);
        if (ret == -EOVERFLOW) {
                snprintf(ir_text, sizeof(ir_text), "N/A(overrange)");
        } else if (ret) {
                report_iio_error("AP3216C", "in_intensity_ir_raw", ret);
                goto unlock;
        } else {
                snprintf(ir_text, sizeof(ir_text), "%d", ir_raw);
        }

        ret = read_iio_int(device_path, "in_proximity_raw", &ps_raw);
        if (ret == -EOVERFLOW) {
                snprintf(ps_text, sizeof(ps_text), "N/A(overrange)");
        } else if (ret) {
                report_iio_error("AP3216C", "in_proximity_raw", ret);
                goto unlock;
        } else {
                snprintf(ps_text, sizeof(ps_text), "%d", ps_raw);
        }

        printf("------------------------------\n"
               "AP3216C | ALS: %.2f lux (raw=%d) | IR: %s | PS: %s\n",
               als_raw * als_scale, als_raw, ir_text, ps_text);

unlock:
        pthread_mutex_unlock(&sample_lock);
}

void task_read_icm20608(void *arg) {
        const char *device_path = (const char *)arg;
        int ax_raw, ay_raw, az_raw;
        int gx_raw, gy_raw, gz_raw;
        int temp_raw;
        float ax, ay, az;
        float gx, gy, gz;
        float temp_c;
        int ret;

        /* 静态变量保持滤波状态，采样锁避免同类任务重叠。 */
        static moving_avg_t fax, fay, faz, fgx, fgy, fgz, ftemp;
        static pthread_mutex_t sample_lock = PTHREAD_MUTEX_INITIALIZER;

        if (!device_path) {
                fprintf(stderr, "ICM20608: IIO device path is NULL\n");
                return;
        }
        if (pthread_mutex_trylock(&sample_lock) != 0)
                return;

        /* 加速度 */
        ret = read_iio_int(device_path, "in_accel_x_raw", &ax_raw);
        if (ret) {
                report_iio_error("ICM20608", "in_accel_x_raw", ret);
                goto unlock;
        }
        ret = read_iio_int(device_path, "in_accel_y_raw", &ay_raw);
        if (ret) {
                report_iio_error("ICM20608", "in_accel_y_raw", ret);
                goto unlock;
        }
        ret = read_iio_int(device_path, "in_accel_z_raw", &az_raw);
        if (ret) {
                report_iio_error("ICM20608", "in_accel_z_raw", ret);
                goto unlock;
        }

        /* 陀螺仪 */
        ret = read_iio_int(device_path, "in_anglvel_x_raw", &gx_raw);
        if (ret) {
                report_iio_error("ICM20608", "in_anglvel_x_raw", ret);
                goto unlock;
        }
        ret = read_iio_int(device_path, "in_anglvel_y_raw", &gy_raw);
        if (ret) {
                report_iio_error("ICM20608", "in_anglvel_y_raw", ret);
                goto unlock;
        }
        ret = read_iio_int(device_path, "in_anglvel_z_raw", &gz_raw);
        if (ret) {
                report_iio_error("ICM20608", "in_anglvel_z_raw", ret);
                goto unlock;
        }

        /* 温度 */
        ret = read_iio_int(device_path, "in_temp_raw", &temp_raw);
        if (ret) {
                report_iio_error("ICM20608", "in_temp_raw", ret);
                goto unlock;
        }

        /* 转换 + 滤波 */
        ax = ma_filter(&fax, ax_raw * 0.000598f / 9.8f);
        ay = ma_filter(&fay, ay_raw * 0.000598f / 9.8f);
        az = ma_filter(&faz, az_raw * 0.000598f / 9.8f);
        gx = ma_filter(&fgx, gx_raw * 0.001065f);
        gy = ma_filter(&fgy, gy_raw * 0.001065f);
        gz = ma_filter(&fgz, gz_raw * 0.001065f);
        temp_c = ma_filter(&ftemp, temp_raw / 326.8f + 25.0f);

        printf("------------------------------\n"
               "ICM20608 | Accel(g): X%+6.3f Y%+6.3f Z%+6.3f | "
               "Gyro(rad/s): X%+7.3f Y%+7.3f Z%+7.3f | Temp: %+6.2f°C\n",
               ax, ay, az, gx, gy, gz, temp_c);

unlock:
        pthread_mutex_unlock(&sample_lock);
}

int creat_periodic_timer(int interval_ms) {
        struct itimerspec its;
        int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
        if (tfd < 0)
                return tfd;

        memset(&its, 0, sizeof(its));
        its.it_interval.tv_sec = interval_ms / 1000;
        its.it_interval.tv_nsec = (interval_ms % 1000) * 1000000;
        its.it_value = its.it_interval;

        if (timerfd_settime(tfd, 0, &its, NULL) < 0) {
                int saved_errno = errno;

                close(tfd);
                errno = saved_errno;
                return -1;
        }

        return tfd;
}

thread_pool_t *thread_pool_create(int max_thread_num) {
        int i = 0;
        int ret;
        if (max_thread_num <= 0) {
                return NULL;
        }

        thread_pool_t *pool = (thread_pool_t *)calloc(1, sizeof(thread_pool_t));
        if (!pool) {
                return NULL;
        }
        pool->threads = calloc(max_thread_num, sizeof(pthread_t));
        if (!pool->threads) {
                free(pool);
                return NULL;
        }
        pool->max_thread_num = max_thread_num;
        pool->head = pool->tail = NULL;
        pool->shutdown = 0;

        pthread_mutex_init(&pool->lock, NULL);
        pthread_cond_init(&pool->cond, NULL);

        for (i = 0; i < max_thread_num; i++) {
                ret = pthread_create(&pool->threads[i], NULL, worker_thread,
                                     pool);
                if (ret != 0) {
                        pthread_mutex_lock(&pool->lock);
                        pool->shutdown = 1;
                        pthread_cond_broadcast(&pool->cond);
                        pthread_mutex_unlock(&pool->lock);

                        while (i > 0) {
                                i--;
                                pthread_join(pool->threads[i], NULL);
                        }
                        pthread_cond_destroy(&pool->cond);
                        pthread_mutex_destroy(&pool->lock);
                        free(pool->threads);
                        free(pool);
                        return NULL;
                }
        }
        return pool;
}

void thread_pool_destroy(thread_pool_t *pool) {
        ssize_t i = 0;
        if (!pool) {
                return;
        }

        pthread_mutex_lock(&pool->lock);
        pool->shutdown = 1;
        pthread_cond_broadcast(&pool->cond);
        pthread_mutex_unlock(&pool->lock);

        for (i = 0; i < pool->max_thread_num; i++) {
                pthread_join(pool->threads[i], NULL);
        }

        task_node_t *cur = pool->head;
        while (cur) {
                task_node_t *next = cur->next;
                free(cur);
                cur = next;
        }

        pthread_cond_destroy(&pool->cond);
        pthread_mutex_destroy(&pool->lock);
        free(pool->threads);
        free(pool);
}

int main(void) {
        char ap3216c_path[IIO_PATH_SIZE];
        char icm20608_path[IIO_PATH_SIZE];
        thread_pool_t *pool;
        uint64_t exp;
        int ret;
        int tfd;
        int ep;

        ret = find_iio_device("ap3216c", ap3216c_path, sizeof(ap3216c_path));
        if (ret) {
                fprintf(stderr, "main: cannot find AP3216C IIO device: %s\n",
                        strerror(-ret));
                return EXIT_FAILURE;
        }

        ret = find_iio_device("icm20608", icm20608_path, sizeof(icm20608_path));
        if (ret) {
                fprintf(stderr, "main: cannot find ICM20608 IIO device: %s\n",
                        strerror(-ret));
                return EXIT_FAILURE;
        }

        printf("IIO devices: ICM20608=%s, AP3216C=%s\n", icm20608_path,
               ap3216c_path);

        pool = thread_pool_create(4);
        if (!pool) {
                fprintf(stderr, "main:创建线程池失败\n");
                return EXIT_FAILURE;
        }

        tfd = creat_periodic_timer(200);
        if (tfd < 0) {
                perror("main: timerfd setup failed");
                thread_pool_destroy(pool);
                return EXIT_FAILURE;
        }

        ep = epoll_create1(0);
        if (ep < 0) {
                perror("main: epoll_create1 失败");
                close(tfd);
                thread_pool_destroy(pool);
                return EXIT_FAILURE;
        }

        struct epoll_event ev = {0};
        ev.events = EPOLLIN;
        ev.data.fd = tfd;
        if (epoll_ctl(ep, EPOLL_CTL_ADD, tfd, &ev) < 0) {
                perror("epoll_ctl error");
                close(ep);
                close(tfd);
                thread_pool_destroy(pool);
                return EXIT_FAILURE;
        }

        for (;;) {
                struct epoll_event e;
                int n = epoll_wait(ep, &e, 1, -1);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        perror("epoll wait fail");
                        break;
                }
                if (e.events & EPOLLIN) {
                        ssize_t bytes_read = read(tfd, &exp, sizeof(exp));
                        if (bytes_read != sizeof(exp)) {
                                if (bytes_read < 0 && errno == EINTR)
                                        continue;
                                if (bytes_read < 0)
                                        perror("timerfd read failed");
                                else
                                        fprintf(stderr,
                                                "timerfd short read: %zd "
                                                "bytes\n",
                                                bytes_read);
                                break;
                        }

                        thread_pool_submit(pool, TASK_READ_AP3216C,
                                           task_read_ap3216c, ap3216c_path);

                        thread_pool_submit(pool, TASK_READ_ICM20608,
                                           task_read_icm20608, icm20608_path);
                }
        }

        close(ep);
        close(tfd);
        thread_pool_destroy(pool);
        return EXIT_FAILURE;
}

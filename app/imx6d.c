#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#define CMD_LED_ON 1
#define CMD_LED_OFF 0
#define DEVICE_FILE_NAME_LED "/dev/led-dts-platform"
#define DEVICE_FILE_NAME_INPUT_KEY "/dev/input/event1"
#define DEVICE_FILE_NAME_AP3216C "/dev/ap3216c"
#define DEVICE_FILE_NAME_ICM20608 "/dev/icm20608"
/* IIO sysfs 路径 */
#define IIO_DEVICE_PATH "/sys/bus/iio/devices/iio:device0"

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
        if (f->count < FILTER_WINDOW) f->count++;
        return f->sum / (float)f->count;
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
        const char *dev = (const char *)arg;
        if (!dev) {
                fprintf(stderr, "task_read_ap3216c:dev path is NULL\r\n");
                return;
        }
        int fd = open(dev, O_RDONLY);
        if (fd < 0) {
                perror("task_read_ap3216c:open dev fail\r\n");
                return;
        }

        unsigned short data[3];
        ssize_t n = read(fd, data, sizeof(data));
        if (n < 0) {
                perror("task_read_ap3216c:read fail\r\n");
                close(fd);
                return;
        }
        if (n < (ssize_t)sizeof(data)) {
                fprintf(stderr,
                        "task_read_ap3216c:read length is not right\r\n");
                close(fd);
                return;
        }

        unsigned short ir = data[0];
        unsigned short als = data[1];
        unsigned short ps = data[2];
        printf("------------------------------\r\n");
        printf("###: AP3216C->IR=%u ALS=%u PS=%u\r\n", ir, als, ps);

        close(fd);
}
void task_read_icm20608(void *arg) {
        char path[256];
        char buf[32];
        int fd, ret;
        int ax_raw, ay_raw, az_raw;
        int gx_raw, gy_raw, gz_raw;
        int temp_raw;
        float ax, ay, az;
        float gx, gy, gz;
        float temp_c;

        /* 静态变量保持滤波状态 */
        static moving_avg_t fax, fay, faz, fgx, fgy, fgz, ftemp;
        static int initialized = 0;
        if (!initialized) {
                int i;
                for (i = 0; i < FILTER_WINDOW; i++) {
                        fax.buf[i] = fay.buf[i] = faz.buf[i] = 0.0f;
                        fgx.buf[i] = fgy.buf[i] = fgz.buf[i] = 0.0f;
                        ftemp.buf[i] = 0.0f;
                }
                fax.idx = fay.idx = faz.idx = 0;
                fgx.idx = fgy.idx = fgz.idx = 0;
                ftemp.idx = 0;
                fax.count = fay.count = faz.count = 0;
                fgx.count = fgy.count = fgz.count = 0;
                ftemp.count = 0;
                fax.sum = fay.sum = faz.sum = 0.0f;
                fgx.sum = fgy.sum = fgz.sum = 0.0f;
                ftemp.sum = 0.0f;
                initialized = 1;
        }

        (void)arg;

        /* 加速度 */
        snprintf(path, sizeof(path), "%s/in_accel_x_raw", IIO_DEVICE_PATH);
        fd = open(path, O_RDONLY);
        if (fd < 0) return;
        ret = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (ret < 0) return;
        buf[ret] = '\0'; ax_raw = atoi(buf);

        snprintf(path, sizeof(path), "%s/in_accel_y_raw", IIO_DEVICE_PATH);
        fd = open(path, O_RDONLY);
        if (fd < 0) return;
        ret = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        buf[ret] = '\0'; ay_raw = atoi(buf);

        snprintf(path, sizeof(path), "%s/in_accel_z_raw", IIO_DEVICE_PATH);
        fd = open(path, O_RDONLY);
        if (fd < 0) return;
        ret = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        buf[ret] = '\0'; az_raw = atoi(buf);

        /* 陀螺仪 */
        snprintf(path, sizeof(path), "%s/in_anglvel_x_raw", IIO_DEVICE_PATH);
        fd = open(path, O_RDONLY);
        if (fd < 0) return;
        ret = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        buf[ret] = '\0'; gx_raw = atoi(buf);

        snprintf(path, sizeof(path), "%s/in_anglvel_y_raw", IIO_DEVICE_PATH);
        fd = open(path, O_RDONLY);
        if (fd < 0) return;
        ret = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        buf[ret] = '\0'; gy_raw = atoi(buf);

        snprintf(path, sizeof(path), "%s/in_anglvel_z_raw", IIO_DEVICE_PATH);
        fd = open(path, O_RDONLY);
        if (fd < 0) return;
        ret = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        buf[ret] = '\0'; gz_raw = atoi(buf);

        /* 温度 */
        snprintf(path, sizeof(path), "%s/in_temp_raw", IIO_DEVICE_PATH);
        fd = open(path, O_RDONLY);
        if (fd < 0) return;
        ret = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        buf[ret] = '\0'; temp_raw = atoi(buf);

        /* 转换 + 滤波 */
        ax = ma_filter(&fax, ax_raw * 0.000598f / 9.8f);
        ay = ma_filter(&fay, ay_raw * 0.000598f / 9.8f);
        az = ma_filter(&faz, az_raw * 0.000598f / 9.8f);
        gx = ma_filter(&fgx, gx_raw * 0.001065f);
        gy = ma_filter(&fgy, gy_raw * 0.001065f);
        gz = ma_filter(&fgz, gz_raw * 0.001065f);
        temp_c = ma_filter(&ftemp, temp_raw / 326.8f + 25.0f);

        printf("------------------------------\r\n");
        printf("ICM20608 | Accel(g): X%+6.3f Y%+6.3f Z%+6.3f | "
               "Gyro(rad/s): X%+7.3f Y%+7.3f Z%+7.3f | Temp: %+6.2f°C\r\n",
               ax, ay, az, gx, gy, gz, temp_c);
}

int creat_periodic_timer(int interval_ms) {
        int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
        if (tfd < 0) {
                return tfd;
        }
        struct itimerspec its; //
        its.it_interval.tv_sec = interval_ms / 1000;
        its.it_value.tv_nsec = (interval_ms % 1000) * 1000000;
        its.it_value = its.it_interval;

        timerfd_settime(tfd, 0, &its, NULL);

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

void daemon_scheduler_loop(thread_pool_t *pool) {}

int main(void) {
        int tfd = 0;
        int ep;

        uint64_t exp;
        thread_pool_t *pool = thread_pool_create(4);
        if (!pool) {
                fprintf(stderr, "main:创建线程池失败\n");
                return -1;
        }
        tfd = creat_periodic_timer(200);
        if (tfd < 0) {
                fprintf(stderr, "main: 创建定时器失败\n");
                thread_pool_destroy(pool);
                return -1;
        }
        ep = epoll_create1(0);
        if (ep < 0) {
                perror("main: epoll_create1 失败");
                close(tfd);
                thread_pool_destroy(pool);
                return -1;
        }
        struct epoll_event ev = {0};
        ev.events = EPOLLIN;
        ev.data.fd = tfd;
        if (epoll_ctl(ep, EPOLL_CTL_ADD, tfd, &ev) < 0) {
                perror("epoll_ctl error!\r\n");
                return -1;
        }

        for (;;) {
                struct epoll_event e;
                int n = epoll_wait(ep, &e, 1, -1);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        perror("epoll wait fail!\r\n");
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
                                                "timerfd short read: %zd bytes\n",
                                                bytes_read);
                                break;
                        }

                        thread_pool_submit(pool, TASK_READ_AP3216C,
                                           task_read_ap3216c,
                                           DEVICE_FILE_NAME_AP3216C);

                        thread_pool_submit(pool, TASK_READ_ICM20608,
                                           task_read_icm20608,
                                           DEVICE_FILE_NAME_ICM20608);
                }
        }

        return 0;
}

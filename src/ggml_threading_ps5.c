#include <pthread.h>

static pthread_mutex_t critical_section = PTHREAD_MUTEX_INITIALIZER;

void ggml_critical_section_start(void)
{
    (void)pthread_mutex_lock(&critical_section);
}

void ggml_critical_section_end(void)
{
    (void)pthread_mutex_unlock(&critical_section);
}

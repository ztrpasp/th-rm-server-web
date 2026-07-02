#include "vendor/rte_array.h"

rte_array_t *
rte_array_create(int max_count, int element_size)
{
    if(max_count<=0 || element_size<=0)
        return NULL;

    rte_array_t *ans = (rte_array_t *)rte_malloc(NULL, sizeof(rte_array_t), 0);
    if(NULL == ans)
        return NULL;
    ans->count = 0;
    ans->max_count = max_count;
    ans->element_size = element_size;
    ans->data = (void *)rte_malloc(NULL, element_size * max_count, 0);
    if(NULL == ans->data)
    {
        rte_free(ans);
        return NULL;
    }
    else
        return ans;
}

int
rte_array_add(rte_array_t *ans, void *element)
{
    if(!ans || !element)
        return -1;
    int size = ans->element_size, count = ans->count, max = ans->max_count;
    if(count < max)
    {
        rte_memcpy(ans->data + count * size, element, size);
        ans->count++;
    }
    // if size not enough, 2 * size
    else
    {
        void *new_data = (void *)rte_malloc(NULL, count * size * 2, 0);
        rte_memcpy(new_data, ans->data, count * size);
        rte_memcpy(new_data + count * size, element, size);
        rte_free(ans->data);
        ans->data = new_data;
        ans->count++;
        ans->max_count *= 2;
    }
    return 1;
}

void *
rte_array_get(rte_array_t *ans, int index)
{
    if(!ans || index < 0 || index >= ans->count)
        return NULL;
    return ans->data + ans->element_size * index;
}

int
rte_array_update(rte_array_t *ans, int index, void *element)
{
    if(!ans || index < 0 || index >= ans->count || !element)
        return -1;
    int size = ans->element_size, count = ans->count, max = ans->max_count;
    rte_memcpy(ans->data + ans->element_size * index,
                element, ans->element_size);
    return 1;
}

int
rte_array_delete(rte_array_t *ans, int index)
{
    if(!ans || index < 0 || index >= ans->count)
        return -1;
    int size = ans->element_size, count = ans->count, max = ans->max_count;
    rte_memcpy(ans->data + size * index,
            ans->data + size * (index + 1), 
            (count - index - 1) * size);
    ans->count--;
    return 1;
}

int
rte_array_loop_function(rte_array_t *ans, int func(void *))
{
    if(!ans || !func)
        return -1;
    int i, count = ans->count, size = ans->element_size;
    for(i = 0; i < count; i++)
        if(func(ans->data + size * i) < 0)
            return 0;
    return 1;
}

int
rte_array_destroy(rte_array_t *ans)
{
    if(!ans)
        return -1;
    rte_free(ans->data);
    rte_free(ans);
    return 0;
}
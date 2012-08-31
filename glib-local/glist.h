#ifndef __G_LIST_H__
#define __G_LIST_H__

typedef int    gint;
typedef unsigned int    guint;
typedef void* gpointer;
typedef const void *gconstpointer;
typedef gint            (*GCompareFunc)         (gconstpointer  a,
                                                 gconstpointer  b);
typedef gint            (*GCompareDataFunc)     (gconstpointer  a,
                                                 gconstpointer  b,
						 gpointer       user_data);
typedef void            (*GFunc)                (gpointer       data,
                                                 gpointer       user_data);
typedef void            (*GDestroyNotify)       (gpointer       data);

struct _GList;
typedef struct _GList GList;

struct _GList
{
  gpointer data;
  GList *next;
  GList *prev;
};

/* Doubly linked lists
 */
void     g_list_free                    (GList            *list);
GList*   g_list_append                  (GList            *list,
					 gpointer          data);
GList*   g_list_delete_link             (GList            *list,
					 GList            *link_);
GList*   g_list_first                   (GList            *list);
GList*   g_list_sort                    (GList            *list,
					 GCompareFunc      compare_func);
guint    g_list_length                  (GList            *list);
void     g_list_foreach                 (GList            *list,
					 GFunc             func,
					 gpointer          user_data);
void     g_list_free_full               (GList            *list,
					 GDestroyNotify    free_func);
GList*   g_list_find_custom             (GList            *list,
					 gconstpointer     data,
					 GCompareFunc      func);
GList*   g_list_remove                  (GList            *list,
                                         gconstpointer     data);

#define g_list_previous(list)	        ((list) ? (((GList *)(list))->prev) : NULL)
#define g_list_next(list)	        ((list) ? (((GList *)(list))->next) : NULL)

#define g_return_val_if_fail(expr,val)	do {			\
     if (expr) { } else						\
       {								\
	 return (val);							\
       }				} while(0);

#endif /* __G_LIST_H__ */

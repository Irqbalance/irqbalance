#include <stdlib.h>

#include "glist.h"

/**
 * g_list_free: 
 * @list: a #GList
 *
 * Frees all of the memory used by a #GList.
 * The freed elements are returned to the slice allocator.
 *
 * <note><para>
 * If list elements contain dynamically-allocated memory, 
 * you should either use g_list_free_full() or free them manually
 * first.
 * </para></note>
 */
void
g_list_free (GList *list)
{
	GList *l = list;

	while(l) {
		GList *tmp = l->next;
		free(l);
		l = tmp;
	}
}

/**
 * g_list_last:
 * @list: a #GList
 *
 * Gets the last element in a #GList.
 *
 * Returns: the last element in the #GList, 
 *     or %NULL if the #GList has no elements
 */
GList*
g_list_last (GList *list)
{
  if (list)
    {
      while (list->next)
	list = list->next;
    }
  
  return list;
}

/**
 * g_list_append:
 * @list: a pointer to a #GList
 * @data: the data for the new element
 *
 * Adds a new element on to the end of the list.
 *
 * <note><para>
 * The return value is the new start of the list, which 
 * may have changed, so make sure you store the new value.
 * </para></note>
 *
 * <note><para>
 * Note that g_list_append() has to traverse the entire list 
 * to find the end, which is inefficient when adding multiple 
 * elements. A common idiom to avoid the inefficiency is to prepend 
 * the elements and reverse the list when all elements have been added.
 * </para></note>
 *
 * |[
 * /&ast; Notice that these are initialized to the empty list. &ast;/
 * GList *list = NULL, *number_list = NULL;
 *
 * /&ast; This is a list of strings. &ast;/
 * list = g_list_append (list, "first");
 * list = g_list_append (list, "second");
 * 
 * /&ast; This is a list of integers. &ast;/
 * number_list = g_list_append (number_list, GINT_TO_POINTER (27));
 * number_list = g_list_append (number_list, GINT_TO_POINTER (14));
 * ]|
 *
 * Returns: the new start of the #GList
 */
GList*
g_list_append (GList	*list,
	       gpointer	 data)
{
  GList *new_list;
  GList *last;
  
  new_list = malloc(sizeof(*new_list));
  new_list->data = data;
  new_list->next = NULL;
  
  if (list)
    {
      last = g_list_last (list);
      /* g_assert (last != NULL); */
      last->next = new_list;
      new_list->prev = last;

      return list;
    }
  else
    {
      new_list->prev = NULL;
      return new_list;
    }
}

static inline GList*
_g_list_remove_link (GList *list,
		     GList *link)
{
  if (link)
    {
      if (link->prev)
	link->prev->next = link->next;
      if (link->next)
	link->next->prev = link->prev;
      
      if (link == list)
	list = list->next;
      
      link->next = NULL;
      link->prev = NULL;
    }
  
  return list;
}

/**
 * g_list_delete_link:
 * @list: a #GList
 * @link_: node to delete from @list
 *
 * Removes the node link_ from the list and frees it. 
 * Compare this to g_list_remove_link() which removes the node 
 * without freeing it.
 *
 * Returns: the new head of @list
 */
GList*
g_list_delete_link (GList *list,
		    GList *link_)
{
  list = _g_list_remove_link (list, link_);
  free (link_);

  return list;
}

/**
 * g_list_first:
 * @list: a #GList
 *
 * Gets the first element in a #GList.
 *
 * Returns: the first element in the #GList, 
 *     or %NULL if the #GList has no elements
 */
GList*
g_list_first (GList *list)
{
  if (list)
    {
      while (list->prev)
	list = list->prev;
    }
  
  return list;
}

static GList *
g_list_sort_merge (GList     *l1, 
		   GList     *l2,
		   GFunc     compare_func,
		   gpointer  user_data)
{
  GList list, *l, *lprev;
  gint cmp;

  l = &list; 
  lprev = NULL;

  while (l1 && l2)
    {
      cmp = ((GCompareDataFunc) compare_func) (l1->data, l2->data, user_data);

      if (cmp <= 0)
        {
	  l->next = l1;
	  l1 = l1->next;
        } 
      else 
	{
	  l->next = l2;
	  l2 = l2->next;
        }
      l = l->next;
      l->prev = lprev; 
      lprev = l;
    }
  l->next = l1 ? l1 : l2;
  l->next->prev = l;

  return list.next;
}

static GList* 
g_list_sort_real (GList    *list,
		  GFunc     compare_func,
		  gpointer  user_data)
{
  GList *l1, *l2;
  
  if (!list) 
    return NULL;
  if (!list->next) 
    return list;
  
  l1 = list; 
  l2 = list->next;

  while ((l2 = l2->next) != NULL)
    {
      if ((l2 = l2->next) == NULL) 
	break;
      l1 = l1->next;
    }
  l2 = l1->next; 
  l1->next = NULL; 

  return g_list_sort_merge (g_list_sort_real (list, compare_func, user_data),
			    g_list_sort_real (l2, compare_func, user_data),
			    compare_func,
			    user_data);
}

/**
 * g_list_sort:
 * @list: a #GList
 * @compare_func: the comparison function used to sort the #GList.
 *     This function is passed the data from 2 elements of the #GList 
 *     and should return 0 if they are equal, a negative value if the 
 *     first element comes before the second, or a positive value if 
 *     the first element comes after the second.
 *
 * Sorts a #GList using the given comparison function.
 *
 * Returns: the start of the sorted #GList
 */
/**
 * GCompareFunc:
 * @a: a value.
 * @b: a value to compare with.
 * @Returns: negative value if @a &lt; @b; zero if @a = @b; positive
 *           value if @a > @b.
 *
 * Specifies the type of a comparison function used to compare two
 * values.  The function should return a negative integer if the first
 * value comes before the second, 0 if they are equal, or a positive
 * integer if the first value comes after the second.
 **/
GList *
g_list_sort (GList        *list,
	     GCompareFunc  compare_func)
{
  return g_list_sort_real (list, (GFunc) compare_func, NULL);
			    
}

/**
 * g_list_length:
 * @list: a #GList
 *
 * Gets the number of elements in a #GList.
 *
 * <note><para>
 * This function iterates over the whole list to 
 * count its elements.
 * </para></note>
 *
 * Returns: the number of elements in the #GList
 */
guint
g_list_length (GList *list)
{
  guint length;
  
  length = 0;
  while (list)
    {
      length++;
      list = list->next;
    }
  
  return length;
}

/**
 * g_list_foreach:
 * @list: a #GList
 * @func: the function to call with each element's data
 * @user_data: user data to pass to the function
 *
 * Calls a function for each element of a #GList.
 */
/**
 * GFunc:
 * @data: the element's data.
 * @user_data: user data passed to g_list_foreach() or
 *             g_slist_foreach().
 *
 * Specifies the type of functions passed to g_list_foreach() and
 * g_slist_foreach().
 **/
void
g_list_foreach (GList	 *list,
		GFunc	  func,
		gpointer  user_data)
{
  while (list)
    {
      GList *next = list->next;
      (*func) (list->data, user_data);
      list = next;
    }
}

/**
 * g_list_free_full:
 * @list: a pointer to a #GList
 * @free_func: the function to be called to free each element's data
 *
 * Convenience method, which frees all the memory used by a #GList, and
 * calls the specified destroy function on every element's data.
 *
 * Since: 2.28
 */
void
g_list_free_full (GList          *list,
		  GDestroyNotify  free_func)
{
  g_list_foreach (list, (GFunc) free_func, NULL);
  g_list_free (list);
}

/**
 * g_list_find_custom:
 * @list: a #GList
 * @data: user data passed to the function
 * @func: the function to call for each element. 
 *     It should return 0 when the desired element is found
 *
 * Finds an element in a #GList, using a supplied function to 
 * find the desired element. It iterates over the list, calling 
 * the given function which should return 0 when the desired 
 * element is found. The function takes two #gconstpointer arguments, 
 * the #GList element's data as the first argument and the 
 * given user data.
 *
 * Returns: the found #GList element, or %NULL if it is not found
 */
GList*
g_list_find_custom (GList         *list,
		    gconstpointer  data,
		    GCompareFunc   func)
{
  g_return_val_if_fail (func != NULL, list);

  while (list)
    {
      if (! func (list->data, data))
	return list;
      list = list->next;
    }

  return NULL;
}

/**
 * g_list_remove:
 * @list: a #GList
 * @data: the data of the element to remove
 *
 * Removes an element from a #GList.
 * If two elements contain the same data, only the first is removed.
 * If none of the elements contain the data, the #GList is unchanged.
 *
 * Returns: the new start of the #GList
 */
GList*
g_list_remove (GList         *list,
               gconstpointer  data)
{
  GList *tmp;
 
  tmp = list;
  while (tmp)
    {
      if (tmp->data != data)
        tmp = tmp->next;
      else
        {
          list = _g_list_remove_link(list, tmp);
          g_list_free(tmp);

          break;
        }
    }
  return list;
}


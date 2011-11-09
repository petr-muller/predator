#include <glib.h>

//***** BEGIN Predator edit *****
#undef  g_assert_cmpint
#define g_assert_cmpint(n1, cmp, n2)
#define g_test_init(a,b,c)
#define g_test_add_func(a,b) b()
#define g_test_run() 1
#define g_test_rand_int_range(a,b) i    // special for use in main() only
#include "gslist.c"
//***** END Predator edit *****

#define SIZE       50
#define NUMBER_MIN 0000
#define NUMBER_MAX 9999


static guint32 array[SIZE];


static gint
sort (gconstpointer p1, gconstpointer p2)
{
  gint32 a, b;

  a = GPOINTER_TO_INT (p1);
  b = GPOINTER_TO_INT (p2);

  return (a > b ? +1 : a == b ? 0 : -1);
}

/*
 * gslist sort tests
 */
static void
test_slist_sort (void)
{
  GSList *slist = NULL;
  gint    i;

  for (i = 0; i < SIZE; i++)
    slist = g_slist_append (slist, GINT_TO_POINTER (array[i]));

  slist = g_slist_sort (slist, sort);
  for (i = 0; i < SIZE - 1; i++)
    {
      gpointer p1, p2;

      p1 = g_slist_nth_data (slist, i);
      p2 = g_slist_nth_data (slist, i+1);

      g_assert (GPOINTER_TO_INT (p1) <= GPOINTER_TO_INT (p2));
    }

  g_slist_free (slist);
}

static void
test_slist_sort_with_data (void)
{
  GSList *slist = NULL;
  gint    i;

  for (i = 0; i < SIZE; i++)
    slist = g_slist_append (slist, GINT_TO_POINTER (array[i]));

  slist = g_slist_sort_with_data (slist, (GCompareDataFunc)sort, NULL);
  for (i = 0; i < SIZE - 1; i++)
    {
      gpointer p1, p2;

      p1 = g_slist_nth_data (slist, i);
      p2 = g_slist_nth_data (slist, i+1);

      g_assert (GPOINTER_TO_INT (p1) <= GPOINTER_TO_INT (p2));
    }

  g_slist_free (slist);
}

static void
test_slist_insert_sorted (void)
{
  GSList *slist = NULL;
  gint    i;

  for (i = 0; i < SIZE; i++)
    slist = g_slist_insert_sorted (slist, GINT_TO_POINTER (array[i]), sort);

  for (i = 0; i < SIZE - 1; i++)
    {
      gpointer p1, p2;

      p1 = g_slist_nth_data (slist, i);
      p2 = g_slist_nth_data (slist, i+1);

      g_assert (GPOINTER_TO_INT (p1) <= GPOINTER_TO_INT (p2));
    }

  g_slist_free (slist);
}

static void
test_slist_insert_sorted_with_data (void)
{
  GSList *slist = NULL;
  gint    i;

  for (i = 0; i < SIZE; i++)
    slist = g_slist_insert_sorted_with_data (slist,
                                           GINT_TO_POINTER (array[i]),
                                           (GCompareDataFunc)sort,
                                           NULL);

  for (i = 0; i < SIZE - 1; i++)
    {
      gpointer p1, p2;

      p1 = g_slist_nth_data (slist, i);
      p2 = g_slist_nth_data (slist, i+1);

      g_assert (GPOINTER_TO_INT (p1) <= GPOINTER_TO_INT (p2));
    }

  g_slist_free (slist);
}

static void
test_slist_reverse (void)
{
  GSList *slist = NULL;
  GSList *st;
  gint    nums[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  gint    i;

  for (i = 0; i < 4; i++)       // Edit: Predator: s/10/4/ for speed
    slist = g_slist_append (slist, &nums[i]);

  slist = g_slist_reverse (slist);

  for (i = 0; i < 10; i++)      // Edit: Predator: s/10/4/
    {
      st = g_slist_nth (slist, i);
      g_assert (*((gint*) st->data) == (9 - i));
    }

  g_slist_free (slist);
}

static void
test_slist_nth (void)
{
  GSList *slist = NULL;
  GSList *st;
  gint    nums[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  gint    i;

  for (i = 0; i < 10; i++)
    slist = g_slist_append (slist, &nums[i]);

  for (i = 0; i < 10; i++)
    {
      st = g_slist_nth (slist, i);
      g_assert (*((gint*) st->data) == i);
    }

  g_slist_free (slist);
}

static void
test_slist_remove (void)
{
  GSList *slist = NULL;
  GSList *st;
  gint    nums[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  gint    i;

  for (i = 0; i < 10; i++)
    {
      slist = g_slist_append (slist, &nums[i]);
      slist = g_slist_append (slist, &nums[i]);
    }

  g_assert_cmpint (g_slist_length (slist), ==, 20);

  for (i = 0; i < 10; i++)
    {
      slist = g_slist_remove (slist, &nums[i]);
    }

  g_assert_cmpint (g_slist_length (slist), ==, 10);

  for (i = 0; i < 10; i++)
    {
      st = g_slist_nth (slist, i);
      g_assert (*((gint*) st->data) == i);
    }

  g_slist_free (slist);
}

static void
test_slist_remove_all (void)
{
  GSList *slist = NULL;
  gint    nums[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  gint    i;

  for (i = 0; i < 10; i++)
    {
      slist = g_slist_append (slist, &nums[i]);
      slist = g_slist_append (slist, &nums[i]);
    }

  g_assert_cmpint (g_slist_length (slist), ==, 20);

  for (i = 0; i < 10; i++)
    {
      slist = g_slist_remove_all (slist, &nums[i]);
    }

  g_assert_cmpint (g_slist_length (slist), ==, 0);
  g_assert (slist == NULL);
}

static void
test_slist_insert (void)
{
  GSList *slist = NULL;
  GSList *st;
  gint   nums[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  gint   i;

  slist = g_slist_insert_before (NULL, NULL, &nums[1]);
  slist = g_slist_insert (slist, &nums[3], 1);
  slist = g_slist_insert (slist, &nums[4], -1);
  slist = g_slist_insert (slist, &nums[0], 0);
  slist = g_slist_insert (slist, &nums[5], 100);
  slist = g_slist_insert_before (slist, NULL, &nums[6]);
  slist = g_slist_insert_before (slist, slist->next->next, &nums[2]);

  slist = g_slist_insert (slist, &nums[9], 7);
  slist = g_slist_insert (slist, &nums[8], 7);
  slist = g_slist_insert (slist, &nums[7], 7);

  for (i = 0; i < 10; i++)
    {
      st = g_slist_nth (slist, i);
      g_assert (*((gint*) st->data) == i);
    }

  g_slist_free (slist);
}

static gint
find_num (gconstpointer l, gconstpointer data)
{
  return *(gint*)l - GPOINTER_TO_INT(data);
}

static void
test_slist_position (void)
{
  GSList *slist = NULL;
  GSList *st;
  gint    nums[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  gint    i;

  for (i = 0; i < 10; i++)
    {
      slist = g_slist_append (slist, &nums[i]);
    }

  g_assert_cmpint (g_slist_index (slist, NULL), ==, -1);
  g_assert_cmpint (g_slist_position (slist, NULL), ==, -1);

  for (i = 0; i < 10; i++)
    {
      g_assert_cmpint (g_slist_index (slist, &nums[i]), ==, i);
      st = g_slist_find_custom (slist, GINT_TO_POINTER(i), find_num);
      g_assert (st != NULL);
      g_assert_cmpint (g_slist_position (slist, st), ==, i);
    }

  g_slist_free (slist);
}

int
main (int argc, char *argv[])
{
  gint i;

  g_test_init (&argc, &argv, NULL);

  /* Create an array of random numbers. */
  for (i = 0; i < SIZE; i++)
    array[i] = g_test_rand_int_range (NUMBER_MIN, NUMBER_MAX);

//  g_test_add_func ("/slist/sort", test_slist_sort);
//  g_test_add_func ("/slist/sort-with-data", test_slist_sort_with_data);
//  g_test_add_func ("/slist/insert-sorted", test_slist_insert_sorted);
//  g_test_add_func ("/slist/insert-sorted-with-data", test_slist_insert_sorted_with_data);
  g_test_add_func ("/slist/reverse", test_slist_reverse);
  g_test_add_func ("/slist/nth", test_slist_nth);
  g_test_add_func ("/slist/remove", test_slist_remove);
  g_test_add_func ("/slist/remove-all", test_slist_remove_all);
  g_test_add_func ("/slist/insert", test_slist_insert);
  g_test_add_func ("/slist/position", test_slist_position);

  return g_test_run ();
}

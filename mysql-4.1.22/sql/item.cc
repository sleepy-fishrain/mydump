/* Copyright (C) 2000 MySQL AB & MySQL Finland AB & TCX DataKonsult AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */


#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation				// gcc: Class implementation
#endif
#include "mysql_priv.h"
#include <m_ctype.h>
#include "my_dir.h"

static void mark_as_dependent(THD *thd,
			      SELECT_LEX *last, SELECT_LEX *current,
			      Item_ident *item);

const String my_null_string("NULL", 4, default_charset_info);

/*****************************************************************************
** Item functions
*****************************************************************************/

/* Init all special items */

void item_init(void)
{
  item_user_lock_init();
}

Item::Item():
  fixed(0)
{
  marker= 0;
  maybe_null=null_value=with_sum_func=unsigned_flag=0;
  collation.set(&my_charset_bin, DERIVATION_COERCIBLE);
  name= 0;
  decimals= 0; max_length= 0;

  /* Put item in free list so that we can free all items at end */
  THD *thd= current_thd;
  next= thd->free_list;
  thd->free_list= this;
  /*
    Item constructor can be called during execution other then SQL_COM
    command => we should check thd->lex->current_select on zero (thd->lex
    can be uninitialised)
  */
  if (thd->lex->current_select)
  {
    enum_parsing_place place= 
      thd->lex->current_select->parsing_place;
    if (place == SELECT_LIST ||
	place == IN_HAVING)
      thd->lex->current_select->select_n_having_items++;
  }
}

/*
  Constructor used by Item_field, Item_*_ref & agregate (sum) functions.
  Used for duplicating lists in processing queries with temporary
  tables
*/
Item::Item(THD *thd, Item *item):
  str_value(item->str_value),
  name(item->name),
  max_length(item->max_length),
  marker(item->marker),
  decimals(item->decimals),
  maybe_null(item->maybe_null),
  null_value(item->null_value),
  unsigned_flag(item->unsigned_flag),
  with_sum_func(item->with_sum_func),
  fixed(item->fixed),
  collation(item->collation)
{
  next= thd->free_list;				// Put in free list
  thd->free_list= this;
}


void Item::print_item_w_name(String *str)
{
  print(str);
  if (name)
  {
    str->append(" AS `", 5);
    str->append(name);
    str->append('`');
  }
}


Item_ident::Item_ident(const char *db_name_par,const char *table_name_par,
		       const char *field_name_par)
  :orig_db_name(db_name_par), orig_table_name(table_name_par), 
   orig_field_name(field_name_par),
   db_name(db_name_par), table_name(table_name_par), 
   field_name(field_name_par), cached_field_index(NO_CACHED_FIELD_INDEX), 
   cached_table(0), depended_from(0)
{
  name = (char*) field_name_par;
}

// Constructor used by Item_field & Item_*_ref (see Item comment)
Item_ident::Item_ident(THD *thd, Item_ident *item)
  :Item(thd, item),
   orig_db_name(item->orig_db_name),
   orig_table_name(item->orig_table_name), 
   orig_field_name(item->orig_field_name),
   db_name(item->db_name),
   table_name(item->table_name),
   field_name(item->field_name),
   cached_field_index(item->cached_field_index),
   cached_table(item->cached_table),
   depended_from(item->depended_from)
{}

void Item_ident::cleanup()
{
  DBUG_ENTER("Item_ident::cleanup");
  DBUG_PRINT("enter", ("b:%s(%s), t:%s(%s), f:%s(%s)",
		       db_name, orig_db_name,
		       table_name, orig_table_name,
		       field_name, orig_field_name));
  Item::cleanup();
  db_name= orig_db_name; 
  table_name= orig_table_name;
  field_name= orig_field_name;
  DBUG_VOID_RETURN;
}

bool Item_ident::remove_dependence_processor(byte * arg)
{
  DBUG_ENTER("Item_ident::remove_dependence_processor");
  if (depended_from == (st_select_lex *) arg)
    depended_from= 0;
  DBUG_RETURN(0);
}


bool Item::check_cols(uint c)
{
  if (c != 1)
  {
    my_error(ER_OPERAND_COLUMNS, MYF(0), c);
    return 1;
  }
  return 0;
}


void Item::set_name(const char *str, uint length, CHARSET_INFO *cs)
{
  if (!length)
  {
    /* Empty string, used by AS or internal function like last_insert_id() */
    name= (char*) str;
    return;
  }
  if (cs->ctype)
  {
    // This will probably need a better implementation in the future:
    // a function in CHARSET_INFO structure.
    while (length && !my_isgraph(cs,*str))
    {						// Fix problem with yacc
      length--;
      str++;
    }
  }
  if (!my_charset_same(cs, system_charset_info))
  {
    uint32 res_length;
    name= sql_strmake_with_convert(str, length, cs,
				   MAX_ALIAS_NAME, system_charset_info,
				   &res_length);
  }
  else
    name=sql_strmake(str, min(length,MAX_ALIAS_NAME));
}


/*
  This function is called when:
  - Comparing items in the WHERE clause (when doing where optimization)
  - When trying to find an ORDER BY/GROUP BY item in the SELECT part
*/

bool Item::eq(const Item *item, bool binary_cmp) const
{
  /*
    Note, that this is never TRUE if item is a Item_param:
    for all basic constants we have special checks, and Item_param's
    type() can be only among basic constant types.
  */
  return type() == item->type() && name && item->name &&
    !my_strcasecmp(system_charset_info,name,item->name);
}


Item *Item::safe_charset_converter(CHARSET_INFO *tocs)
{
  Item_func_conv_charset *conv= new Item_func_conv_charset(this, tocs, 1);
  return conv->safe ? conv : NULL;
}


/*
  Created mostly for mysql_prepare_table(). Important
  when a string ENUM/SET column is described with a numeric default value:

  CREATE TABLE t1(a SET('a') DEFAULT 1);

  We cannot use generic Item::safe_charset_converter(), because
  the latter returns a non-fixed Item, so val_str() crashes afterwards.
  Override Item_num method, to return a fixed item.
*/
Item *Item_num::safe_charset_converter(CHARSET_INFO *tocs)
{
  Item_string *conv;
  char buf[64];
  String *s, tmp(buf, sizeof(buf), &my_charset_bin);
  s= val_str(&tmp);
  if ((conv= new Item_string(s->ptr(), s->length(), s->charset())))
  {
    conv->str_value.copy();
    conv->str_value.shrink_to_length();
  }
  return conv;
}


Item *Item_string::safe_charset_converter(CHARSET_INFO *tocs)
{
  Item_string *conv;
  uint conv_errors;
  String tmp, cstr, *ostr= val_str(&tmp);
  cstr.copy(ostr->ptr(), ostr->length(), ostr->charset(), tocs, &conv_errors);
  if (conv_errors || !(conv= new Item_string(cstr.ptr(), cstr.length(),
                                             cstr.charset(),
                                             collation.derivation)))
  {
    /*
      Safe conversion is not possible (or EOM).
      We could not convert a string into the requested character set
      without data loss. The target charset does not cover all the
      characters from the string. Operation cannot be done correctly.
    */
    return NULL;
  }
  conv->str_value.copy();
  /* 
    The above line executes str_value.realloc() internally,
    which alligns Alloced_length using ALLIGN_SIZE.
    In the case of Item_string::str_value we don't want
    Alloced_length to be longer than str_length.
    Otherwise, some functions like Item_func_concat::val_str()
    try to reuse str_value as a buffer for concatenation result
    for optimization purposes, so our string constant become
    corrupted. See bug#8785 for more details.
    Let's shrink Alloced_length to str_length to avoid this problem.
  */
  conv->str_value.shrink_to_length();
  return conv;
}


Item *Item_param::safe_charset_converter(CHARSET_INFO *tocs)
{
  if (const_item())
  {
    Item_string *conv;
    uint conv_errors;
    char buf[MAX_FIELD_WIDTH];
    String tmp(buf, sizeof(buf), &my_charset_bin);
    String cstr, *ostr= val_str(&tmp);
    /*
      As safe_charset_converter is not executed for
      a parameter bound to NULL, ostr should never be 0.
    */
    cstr.copy(ostr->ptr(), ostr->length(), ostr->charset(), tocs, &conv_errors);
    if (conv_errors || !(conv= new Item_string(cstr.ptr(), cstr.length(),
                                               cstr.charset(),
                                               collation.derivation)))
      return NULL;
    conv->str_value.copy();
    conv->str_value.shrink_to_length();
    return conv;
  }
  return NULL;
}


bool Item_string::eq(const Item *item, bool binary_cmp) const
{
  if (type() == item->type() && item->basic_const_item())
  {
    if (binary_cmp)
      return !stringcmp(&str_value, &item->str_value);
    return !sortcmp(&str_value, &item->str_value, collation.collation);
  }
  return 0;
}


/*
  Get the value of the function as a TIME structure.
  As a extra convenience the time structure is reset on error!
 */

bool Item::get_date(TIME *ltime,uint fuzzydate)
{
  char buff[40];
  String tmp(buff,sizeof(buff), &my_charset_bin),*res;
  if (!(res=val_str(&tmp)) ||
      str_to_datetime_with_warn(res->ptr(), res->length(),
                                ltime, fuzzydate) <= MYSQL_TIMESTAMP_ERROR)
  {
    bzero((char*) ltime,sizeof(*ltime));
    return 1;
  }
  return 0;
}

/*
  Get time of first argument.
  As a extra convenience the time structure is reset on error!
 */

bool Item::get_time(TIME *ltime)
{
  char buff[40];
  String tmp(buff,sizeof(buff),&my_charset_bin),*res;
  if (!(res=val_str(&tmp)) ||
      str_to_time_with_warn(res->ptr(), res->length(), ltime))
  {
    bzero((char*) ltime,sizeof(*ltime));
    return 1;
  }
  return 0;
}

CHARSET_INFO *Item::default_charset()
{
  return current_thd->variables.collation_connection;
}


/*
  Move SUM items out from item tree and replace with reference

  SYNOPSIS
    split_sum_func2()
    thd			Thread handler
    ref_pointer_array	Pointer to array of reference fields
    fields		All fields in select
    ref			Pointer to item

  NOTES
   This is from split_sum_func2() for items that should be split

   All found SUM items are added FIRST in the fields list and
   we replace the item with a reference.

   thd->fatal_error() may be called if we are out of memory
*/


void Item::split_sum_func2(THD *thd, Item **ref_pointer_array,
                           List<Item> &fields, Item **ref)
{
  if (type() != SUM_FUNC_ITEM && with_sum_func)
  {
    /* Will split complicated items and ignore simple ones */
    split_sum_func(thd, ref_pointer_array, fields);
  }
  else if ((type() == SUM_FUNC_ITEM ||
            (used_tables() & ~PARAM_TABLE_BIT)) &&
           type() != SUBSELECT_ITEM &&
           type() != REF_ITEM)
  {
    /*
      Replace item with a reference so that we can easily calculate
      it (in case of sum functions) or copy it (in case of fields)

      The test above is to ensure we don't do a reference for things
      that are constants (PARAM_TABLE_BIT is in effect a constant)
      or already referenced (for example an item in HAVING)
    */
    uint el= fields.elements;
    Item *new_item;    
    ref_pointer_array[el]= this;
    if (!(new_item= new Item_ref(ref_pointer_array + el, 0, name)))
      return;                                   // fatal_error is set
    fields.push_front(this);
    ref_pointer_array[el]= this;
    thd->change_item_tree(ref, new_item);
  }
}


/*
   Aggregate two collations together taking
   into account their coercibility (aka derivation):

   0 == DERIVATION_EXPLICIT  - an explicitely written COLLATE clause
   1 == DERIVATION_NONE      - a mix of two different collations
   2 == DERIVATION_IMPLICIT  - a column
   3 == DERIVATION_COERCIBLE - a string constant

   The most important rules are:

   1. If collations are the same:
      chose this collation, and the strongest derivation.

   2. If collations are different:
     - Character sets may differ, but only if conversion without
       data loss is possible. The caller provides flags whether
       character set conversion attempts should be done. If no
       flags are substituted, then the character sets must be the same.
       Currently processed flags are:
         MY_COLL_ALLOW_SUPERSET_CONV  - allow conversion to a superset
         MY_COLL_ALLOW_COERCIBLE_CONV - allow conversion of a coercible value
     - two EXPLICIT collations produce an error, e.g. this is wrong:
       CONCAT(expr1 collate latin1_swedish_ci, expr2 collate latin1_german_ci)
     - the side with smaller derivation value wins,
       i.e. a column is stronger than a string constant,
       an explicit COLLATE clause is stronger than a column.
     - if derivations are the same, we have DERIVATION_NONE,
       we'll wait for an explicit COLLATE clause which possibly can
       come from another argument later: for example, this is valid,
       but we don't know yet when collecting the first two arguments:
         CONCAT(latin1_swedish_ci_column,
                latin1_german1_ci_column,
                expr COLLATE latin1_german2_ci)
*/
bool DTCollation::aggregate(DTCollation &dt, uint flags)
{
  if (!my_charset_same(collation, dt.collation))
  {
    /* 
       We do allow to use binary strings (like BLOBS)
       together with character strings.
       Binaries have more precedance than a character
       string of the same derivation.
    */
    if (collation == &my_charset_bin)
    {
      if (derivation <= dt.derivation)
	; // Do nothing
      else
      {
	set(dt); 
      }
    }
    else if (dt.collation == &my_charset_bin)
    {
      if (dt.derivation <= derivation)
      {
        set(dt);
      }
      else
       ; // Do nothing
    }
    else if ((flags & MY_COLL_ALLOW_SUPERSET_CONV) &&
             collation->state & MY_CS_UNICODE &&
             (derivation < dt.derivation ||
             (derivation == dt.derivation &&
             !(dt.collation->state & MY_CS_UNICODE))))
    {
      // Do nothing
    }
    else if ((flags & MY_COLL_ALLOW_SUPERSET_CONV) &&
             dt.collation->state & MY_CS_UNICODE &&
             (dt.derivation < derivation ||
              (dt.derivation == derivation &&
             !(collation->state & MY_CS_UNICODE))))
    {
      set(dt);
    }
    else if ((flags & MY_COLL_ALLOW_COERCIBLE_CONV) &&
             derivation < dt.derivation &&
             dt.derivation >= DERIVATION_SYSCONST)
    {
      // Do nothing;
    }
    else if ((flags & MY_COLL_ALLOW_COERCIBLE_CONV) &&
             dt.derivation < derivation &&
             derivation >= DERIVATION_SYSCONST)
    {
      set(dt);
    }
    else
    {
      // Cannot apply conversion
      set(0, DERIVATION_NONE);
      return 1;
    }
  }
  else if (derivation < dt.derivation)
  {
    // Do nothing
  }
  else if (dt.derivation < derivation)
  {
    set(dt);
  }
  else
  { 
    if (collation == dt.collation)
    {
      // Do nothing
    }
    else 
    {
      if (derivation == DERIVATION_EXPLICIT)
      {
	set(0, DERIVATION_NONE);
	return 1;
      }
      if (collation->state & MY_CS_BINSORT)
      {
        return 0;
      }
      else if (dt.collation->state & MY_CS_BINSORT)
      {
        set(dt);
        return 0;
      }
      CHARSET_INFO *bin= get_charset_by_csname(collation->csname, 
                                               MY_CS_BINSORT,MYF(0));
      set(bin, DERIVATION_NONE);
    }
  }
  return 0;
}

/******************************/
static
void my_coll_agg_error(DTCollation &c1, DTCollation &c2, const char *fname)
{
  my_error(ER_CANT_AGGREGATE_2COLLATIONS,MYF(0),
           c1.collation->name,c1.derivation_name(),
           c2.collation->name,c2.derivation_name(),
           fname);
}


static
void my_coll_agg_error(DTCollation &c1, DTCollation &c2, DTCollation &c3,
                       const char *fname)
{
  my_error(ER_CANT_AGGREGATE_3COLLATIONS,MYF(0),
  	   c1.collation->name,c1.derivation_name(),
	   c2.collation->name,c2.derivation_name(),
	   c3.collation->name,c3.derivation_name(),
	   fname);
}


static
void my_coll_agg_error(Item** args, uint count, const char *fname)
{
  if (count == 2)
    my_coll_agg_error(args[0]->collation, args[1]->collation, fname);
  else if (count == 3)
    my_coll_agg_error(args[0]->collation, args[1]->collation,
                      args[2]->collation, fname);
  else
    my_error(ER_CANT_AGGREGATE_NCOLLATIONS,MYF(0),fname);
}


bool agg_item_collations(DTCollation &c, const char *fname,
                         Item **av, uint count, uint flags)
{
  uint i;
  c.set(av[0]->collation);
  for (i= 1; i < count; i++)
  {
    if (c.aggregate(av[i]->collation, flags))
    {
      my_coll_agg_error(av, count, fname);
      return TRUE;
    }
  }
  if ((flags & MY_COLL_DISALLOW_NONE) &&
      c.derivation == DERIVATION_NONE)
  {
    my_coll_agg_error(av, count, fname);
    return TRUE;
  }
  return FALSE;
}


bool agg_item_collations_for_comparison(DTCollation &c, const char *fname,
                                        Item **av, uint count, uint flags)
{
  return (agg_item_collations(c, fname, av, count,
                              flags | MY_COLL_DISALLOW_NONE));
}


/* 
  Collect arguments' character sets together.
  We allow to apply automatic character set conversion in some cases.
  The conditions when conversion is possible are:
  - arguments A and B have different charsets
  - A wins according to coercibility rules
    (i.e. a column is stronger than a string constant,
     an explicit COLLATE clause is stronger than a column)
  - character set of A is either superset for character set of B,
    or B is a string constant which can be converted into the
    character set of A without data loss.
    
  If all of the above is true, then it's possible to convert
  B into the character set of A, and then compare according
  to the collation of A.
  
  For functions with more than two arguments:

    collect(A,B,C) ::= collect(collect(A,B),C)
*/

bool agg_item_charsets(DTCollation &coll, const char *fname,
                       Item **args, uint nargs, uint flags)
{
  Item **arg, **last, *safe_args[2];
  if (agg_item_collations(coll, fname, args, nargs, flags))
    return TRUE;

  /*
    For better error reporting: save the first and the second argument.
    We need this only if the the number of args is 3 or 2:
    - for a longer argument list, "Illegal mix of collations"
      doesn't display each argument's characteristics.
    - if nargs is 1, then this error cannot happen.
  */
  if (nargs >=2 && nargs <= 3)
  {
    safe_args[0]= args[0];
    safe_args[1]= args[1];
  }

  THD *thd= current_thd;
  Item_arena *arena, backup;
  bool res= FALSE;
  /*
    In case we're in statement prepare, create conversion item
    in its memory: it will be reused on each execute.
  */
  arena= thd->change_arena_if_needed(&backup);

  for (arg= args, last= args + nargs; arg < last; arg++)
  {
    Item* conv;
    uint32 dummy_offset;
    if (!String::needs_conversion(0, coll.collation,
                                  (*arg)->collation.collation,
                                  &dummy_offset))
      continue;

    if (!(conv= (*arg)->safe_charset_converter(coll.collation)))
    {
      if (nargs >=2 && nargs <= 3)
      {
        /* restore the original arguments for better error message */
        args[0]= safe_args[0];
        args[1]= safe_args[1];
      }
      my_coll_agg_error(args, nargs, fname);
      res= TRUE;
      break; // we cannot return here, we need to restore "arena".
    }
    conv->fix_fields(thd, 0, &conv);
    /*
      If in statement prepare, then we create a converter for two
      constant items, do it once and then reuse it.
      If we're in execution of a prepared statement, arena is NULL,
      and the conv was created in runtime memory. This can be
      the case only if the argument is a parameter marker ('?'),
      because for all true constants the charset converter has already
      been created in prepare. In this case register the change for
      rollback.
    */
    if (arena)
      *arg= conv;
    else
      thd->change_item_tree(arg, conv);
  }
  if (arena)
    thd->restore_backup_item_arena(arena, &backup);
  return res;
}




/**********************************************/

Item_field::Item_field(Field *f)
  :Item_ident(NullS, f->table_name, f->field_name)
{
  set_field(f);
  /*
    field_name and talbe_name should not point to garbage
    if this item is to be reused
  */
  orig_table_name= orig_field_name= "";
}

Item_field::Item_field(THD *thd, Field *f)
  :Item_ident(f->table->table_cache_key, f->table_name, f->field_name)
{
  /*
    We always need to provide Item_field with a fully qualified field
    name to avoid ambiguity when executing prepared statements like
    SELECT * from d1.t1, d2.t1; (assuming d1.t1 and d2.t1 have columns
    with same names).
    This is because prepared statements never deal with wildcards in
    select list ('*') and always fix fields using fully specified path
    (i.e. db.table.column).
    No check for OOM: if db_name is NULL, we'll just get
    "Field not found" error.
    We need to copy db_name, table_name and field_name because they must
    be allocated in the statement memory, not in table memory (the table
    structure can go away and pop up again between subsequent executions
    of a prepared statement).
  */
  if (thd->current_arena->is_stmt_prepare())
  {
    if (db_name)
      orig_db_name= thd->strdup(db_name);
    orig_table_name= thd->strdup(table_name);
    orig_field_name= thd->strdup(field_name);
    /*
      We don't restore 'name' in cleanup because it's not changed
      during execution. Still we need it to point to persistent
      memory if this item is to be reused.
    */
    name= (char*) orig_field_name;
  }
  set_field(f);
}

// Constructor need to process subselect with temporary tables (see Item)
Item_field::Item_field(THD *thd, Item_field *item)
  :Item_ident(thd, item),
   field(item->field),
   result_field(item->result_field)
{
  collation.set(DERIVATION_IMPLICIT);
}

void Item_field::set_field(Field *field_par)
{
  field=result_field=field_par;			// for easy coding with fields
  maybe_null=field->maybe_null();
  max_length=field_par->max_length();
  decimals= field->decimals();
  table_name=field_par->table_name;
  field_name=field_par->field_name;
  db_name=field_par->table->table_cache_key;
  unsigned_flag=test(field_par->flags & UNSIGNED_FLAG);
  collation.set(field_par->charset(), DERIVATION_IMPLICIT);
  fixed= 1;
}


/*
  Reset this item to point to a field from the new temporary table.
  This is used when we create a new temporary table for each execution
  of prepared statement.
*/

void Item_field::reset_field(Field *f)
{
  set_field(f);
  /* 'name' is pointing at field->field_name of old field */
  name= (char*) f->field_name;
}

const char *Item_ident::full_name() const
{
  char *tmp;
  if (!table_name || !field_name)
    return field_name ? field_name : name ? name : "tmp_field";
  if (db_name && db_name[0])
  {
    tmp=(char*) sql_alloc((uint) strlen(db_name)+(uint) strlen(table_name)+
			  (uint) strlen(field_name)+3);
    strxmov(tmp,db_name,".",table_name,".",field_name,NullS);
  }
  else
  {
    if (table_name[0])
    {
      tmp= (char*) sql_alloc((uint) strlen(table_name) +
			     (uint) strlen(field_name) + 2);
      strxmov(tmp, table_name, ".", field_name, NullS);
    }
    else
      tmp= (char*) field_name;
  }
  return tmp;
}

/* ARGSUSED */
String *Item_field::val_str(String *str)
{
  DBUG_ASSERT(fixed == 1);
  if ((null_value=field->is_null()))
    return 0;
  str->set_charset(str_value.charset());
  return field->val_str(str,&str_value);
}

double Item_field::val()
{
  DBUG_ASSERT(fixed == 1);
  if ((null_value=field->is_null()))
    return 0.0;
  return field->val_real();
}

longlong Item_field::val_int()
{
  DBUG_ASSERT(fixed == 1);
  if ((null_value=field->is_null()))
    return 0;
  return field->val_int();
}


String *Item_field::str_result(String *str)
{
  if ((null_value=result_field->is_null()))
    return 0;
  str->set_charset(str_value.charset());
  return result_field->val_str(str,&str_value);
}

bool Item_field::get_date(TIME *ltime,uint fuzzydate)
{
  if ((null_value=field->is_null()) || field->get_date(ltime,fuzzydate))
  {
    bzero((char*) ltime,sizeof(*ltime));
    return 1;
  }
  return 0;
}

bool Item_field::get_date_result(TIME *ltime,uint fuzzydate)
{
  if ((null_value=result_field->is_null()) ||
      result_field->get_date(ltime,fuzzydate))
  {
    bzero((char*) ltime,sizeof(*ltime));
    return 1;
  }
  return 0;
}

bool Item_field::get_time(TIME *ltime)
{
  if ((null_value=field->is_null()) || field->get_time(ltime))
  {
    bzero((char*) ltime,sizeof(*ltime));
    return 1;
  }
  return 0;
}

double Item_field::val_result()
{
  if ((null_value=result_field->is_null()))
    return 0.0;
  return result_field->val_real();
}

longlong Item_field::val_int_result()
{
  if ((null_value=result_field->is_null()))
    return 0;
  return result_field->val_int();
}


bool Item_field::eq(const Item *item, bool binary_cmp) const
{
  if (item->type() != FIELD_ITEM)
    return 0;
  
  Item_field *item_field= (Item_field*) item;
  if (item_field->field)
    return item_field->field == field;
  /*
    We may come here when we are trying to find a function in a GROUP BY
    clause from the select list.
    In this case the '100 % correct' way to do this would be to first
    run fix_fields() on the GROUP BY item and then retry this function, but
    I think it's better to relax the checking a bit as we will in
    most cases do the correct thing by just checking the field name.
    (In cases where we would choose wrong we would have to generate a
    ER_NON_UNIQ_ERROR).
  */
  return (!my_strcasecmp(system_charset_info, item_field->name,
			 field_name) &&
	  (!item_field->table_name ||
	   (!my_strcasecmp(table_alias_charset, item_field->table_name,
			   table_name) &&
	    (!item_field->db_name ||
	     (item_field->db_name && !strcmp(item_field->db_name,
					     db_name))))));
}


table_map Item_field::used_tables() const
{
  if (field->table->const_table)
    return 0;					// const item
  return (depended_from ? OUTER_REF_TABLE_BIT : field->table->map);
}


Item *Item_field::get_tmp_table_item(THD *thd)
{
  Item_field *new_item= new Item_field(thd, this);
  if (new_item)
    new_item->field= new_item->result_field;
  return new_item;
}


/*
  Create an item from a string we KNOW points to a valid longlong/ulonglong
  end \0 terminated number string
*/

Item_int::Item_int(const char *str_arg, uint length)
{
  char *end_ptr= (char*) str_arg + length;
  int error;
  value= my_strtoll10(str_arg, &end_ptr, &error);
  max_length= (uint) (end_ptr - str_arg);
  name= (char*) str_arg;
  fixed= 1;
}


String *Item_int::val_str(String *str)
{
  // following assert is redundant, because fixed=1 assigned in constructor
  DBUG_ASSERT(fixed == 1);
  str->set(value, &my_charset_bin);
  return str;
}

void Item_int::print(String *str)
{
  // my_charset_bin is good enough for numbers
  str_value.set(value, &my_charset_bin);
  str->append(str_value);
}


Item_uint::Item_uint(const char *str_arg, uint length):
  Item_int(str_arg, length)
{
  unsigned_flag= 1;
}


Item_uint::Item_uint(const char *str_arg, longlong i, uint length):
  Item_int(str_arg, i, length)
{
  unsigned_flag= 1;
}


String *Item_uint::val_str(String *str)
{
  // following assert is redundant, because fixed=1 assigned in constructor
  DBUG_ASSERT(fixed == 1);
  str->set((ulonglong) value, &my_charset_bin);
  return str;
}


void Item_uint::print(String *str)
{
  // latin1 is good enough for numbers
  str_value.set((ulonglong) value, default_charset());
  str->append(str_value);
}


String *Item_real::val_str(String *str)
{
  // following assert is redundant, because fixed=1 assigned in constructor
  DBUG_ASSERT(fixed == 1);
  str->set(value,decimals,&my_charset_bin);
  return str;
}


void Item_string::print(String *str)
{
  str->append('_');
  str->append(collation.collation->csname);
  str->append('\'');
  str_value.print(str);
  str->append('\'');
}

bool Item_null::eq(const Item *item, bool binary_cmp) const
{ return item->type() == type(); }
double Item_null::val()
{
  // following assert is redundant, because fixed=1 assigned in constructor
  DBUG_ASSERT(fixed == 1);
  null_value=1;
  return 0.0;
}
longlong Item_null::val_int()
{
  // following assert is redundant, because fixed=1 assigned in constructor
  DBUG_ASSERT(fixed == 1);
  null_value=1;
  return 0;
}
/* ARGSUSED */
String *Item_null::val_str(String *str)
{
  // following assert is redundant, because fixed=1 assigned in constructor
  DBUG_ASSERT(fixed == 1);
  null_value=1;
  return 0;
}


Item *Item_null::safe_charset_converter(CHARSET_INFO *tocs)
{
  collation.set(tocs);
  return this;
}

/*********************** Item_param related ******************************/

/* 
  Default function of Item_param::set_param_func, so in case
  of malformed packet the server won't SIGSEGV
*/

static void
default_set_param_func(Item_param *param,
                       uchar **pos __attribute__((unused)),
                       ulong len __attribute__((unused)))
{
  param->set_null();
}

Item_param::Item_param(unsigned pos_in_query_arg) :
  state(NO_VALUE),
  item_result_type(STRING_RESULT),
  /* Don't pretend to be a literal unless value for this item is set. */
  item_type(PARAM_ITEM),
  param_type(MYSQL_TYPE_STRING),
  pos_in_query(pos_in_query_arg),
  set_param_func(default_set_param_func)
{
  name= (char*) "?";
  /* 
    Since we can't say whenever this item can be NULL or cannot be NULL
    before mysql_stmt_execute(), so we assuming that it can be NULL until
    value is set.
  */
  maybe_null= 1;
}

void Item_param::set_null()
{
  DBUG_ENTER("Item_param::set_null");
  /* These are cleared after each execution by reset() method */
  max_length= 0;
  null_value= 1;
  /* 
    Because of NULL and string values we need to set max_length for each new
    placeholder value: user can submit NULL for any placeholder type, and 
    string length can be different in each execution.
  */
  max_length= 0;
  decimals= 0;
  state= NULL_VALUE;
  item_type= Item::NULL_ITEM;
  DBUG_VOID_RETURN;
}

void Item_param::set_int(longlong i, uint32 max_length_arg)
{
  DBUG_ENTER("Item_param::set_int");
  value.integer= (longlong) i;
  state= INT_VALUE;
  max_length= max_length_arg;
  decimals= 0;
  maybe_null= 0;
  DBUG_VOID_RETURN;
}

void Item_param::set_double(double d)
{
  DBUG_ENTER("Item_param::set_double");
  value.real= d;
  state= REAL_VALUE;
  max_length= DBL_DIG + 8;
  decimals= NOT_FIXED_DEC;
  maybe_null= 0;
  DBUG_VOID_RETURN;
}


/*
  Set parameter value from TIME value.

  SYNOPSIS
    set_time()
      tm             - datetime value to set (time_type is ignored)
      type           - type of datetime value
      max_length_arg - max length of datetime value as string

  NOTE
    If we value to be stored is not normalized, zero value will be stored
    instead and proper warning will be produced. This function relies on
    the fact that even wrong value sent over binary protocol fits into
    MAX_DATE_STRING_REP_LENGTH buffer.
*/
void Item_param::set_time(TIME *tm, timestamp_type type, uint32 max_length_arg)
{ 
  DBUG_ENTER("Item_param::set_time");

  value.time= *tm;
  value.time.time_type= type;

  if (value.time.year > 9999 || value.time.month > 12 ||
      value.time.day > 31 ||
      type != MYSQL_TIMESTAMP_TIME && value.time.hour > 23 ||
      value.time.minute > 59 || value.time.second > 59)
  {
    char buff[MAX_DATE_STRING_REP_LENGTH];
    uint length= my_TIME_to_str(&value.time, buff);
    make_truncated_value_warning(current_thd, buff, length, type);
    set_zero_time(&value.time, MYSQL_TIMESTAMP_ERROR);
  }

  state= TIME_VALUE;
  maybe_null= 0;
  max_length= max_length_arg;
  decimals= 0;
  DBUG_VOID_RETURN;
}


bool Item_param::set_str(const char *str, ulong length)
{
  DBUG_ENTER("Item_param::set_str");
  /*
    Assign string with no conversion: data is converted only after it's
    been written to the binary log.
  */
  uint dummy_errors;
  if (str_value.copy(str, length, &my_charset_bin, &my_charset_bin,
                     &dummy_errors))
    DBUG_RETURN(TRUE);
  state= STRING_VALUE;
  maybe_null= 0;
  /* max_length and decimals are set after charset conversion */
  /* sic: str may be not null-terminated, don't add DBUG_PRINT here */
  DBUG_RETURN(FALSE);
}


bool Item_param::set_longdata(const char *str, ulong length)
{
  DBUG_ENTER("Item_param::set_longdata");

  /*
    If client character set is multibyte, end of long data packet
    may hit at the middle of a multibyte character.  Additionally,
    if binary log is open we must write long data value to the
    binary log in character set of client. This is why we can't
    convert long data to connection character set as it comes
    (here), and first have to concatenate all pieces together,
    write query to the binary log and only then perform conversion.
  */
  if (str_value.append(str, length, &my_charset_bin))
    DBUG_RETURN(TRUE);
  state= LONG_DATA_VALUE;
  maybe_null= 0;

  DBUG_RETURN(FALSE);
}


/*
  Set parameter value from user variable value.

  SYNOPSIS
   set_from_user_var
     thd   Current thread
     entry User variable structure (NULL means use NULL value)

  RETURN
    0 OK
    1 Out of memort
*/

bool Item_param::set_from_user_var(THD *thd, const user_var_entry *entry)
{
  DBUG_ENTER("Item_param::set_from_user_var");
  if (entry && entry->value)
  {
    item_result_type= entry->type;
    switch (entry->type) {
    case REAL_RESULT:
      set_double(*(double*)entry->value);
      item_type= Item::REAL_ITEM;
      item_result_type= REAL_RESULT;
      break;
    case INT_RESULT:
      set_int(*(longlong*)entry->value, 21);
      item_type= Item::INT_ITEM;
      item_result_type= INT_RESULT;
      break;
    case STRING_RESULT:
    {
      CHARSET_INFO *fromcs= entry->collation.collation;
      CHARSET_INFO *tocs= thd->variables.collation_connection;
      uint32 dummy_offset;

      value.cs_info.character_set_of_placeholder= fromcs;
      /*
        Setup source and destination character sets so that they
        are different only if conversion is necessary: this will
        make later checks easier.
      */
      value.cs_info.final_character_set_of_str_value=
        String::needs_conversion(0, fromcs, tocs, &dummy_offset) ?
        tocs : fromcs;
      /*
        Exact value of max_length is not known unless data is converted to
        charset of connection, so we have to set it later.
      */
      item_type= Item::STRING_ITEM;
      item_result_type= STRING_RESULT;

      if (set_str((const char *)entry->value, entry->length))
        DBUG_RETURN(1);
      break;
    }
    default:
      DBUG_ASSERT(0);
      set_null();
    }
  }
  else
    set_null();

  DBUG_RETURN(0);
}

/*
    Resets parameter after execution.
  
  SYNOPSIS
     Item_param::reset()
 
  NOTES
    We clear null_value here instead of setting it in set_* methods, 
    because we want more easily handle case for long data.
*/

void Item_param::reset()
{
  /* Shrink string buffer if it's bigger than max possible CHAR column */
  if (str_value.alloced_length() > MAX_CHAR_WIDTH)
    str_value.free();
  else
    str_value.length(0);
  str_value_ptr.length(0);
  /*
    We must prevent all charset conversions untill data has been written
    to the binary log.
  */
  str_value.set_charset(&my_charset_bin);
  collation.set(&my_charset_bin, DERIVATION_COERCIBLE);
  state= NO_VALUE;
  maybe_null= 1;
  null_value= 0;
  /*
    Don't reset item_type to PARAM_ITEM: it's only needed to guard
    us from item optimizations at prepare stage, when item doesn't yet
    contain a literal of some kind.
    In all other cases when this object is accessed its value is
    set (this assumption is guarded by 'state' and
    DBUG_ASSERTS(state != NO_VALUE) in all Item_param::get_*
    methods).
  */
}


int Item_param::save_in_field(Field *field, bool no_conversions)
{
  field->set_notnull();

  switch (state) {
  case INT_VALUE:
    return field->store(value.integer);
  case REAL_VALUE:
    return field->store(value.real);
  case TIME_VALUE:
    field->store_time(&value.time, value.time.time_type);
    return 0;
  case STRING_VALUE:
  case LONG_DATA_VALUE:
    return field->store(str_value.ptr(), str_value.length(),
                        str_value.charset());
  case NULL_VALUE:
    return set_field_to_null_with_conversions(field, no_conversions);
  case NO_VALUE:
  default:
    DBUG_ASSERT(0);
  }
  return 1;
}


bool Item_param::get_time(TIME *res)
{
  if (state == TIME_VALUE)
  {
    *res= value.time;
    return 0;
  }
  /*
    If parameter value isn't supplied assertion will fire in val_str()
    which is called from Item::get_time().
  */
  return Item::get_time(res);
}


bool Item_param::get_date(TIME *res, uint fuzzydate)
{
  if (state == TIME_VALUE)
  {
    *res= value.time;
    return 0;
  }
  return Item::get_date(res, fuzzydate);
}


double Item_param::val() 
{
  switch (state) {
  case REAL_VALUE:
    return value.real;
  case INT_VALUE:
    return (double) value.integer;
  case STRING_VALUE:
  case LONG_DATA_VALUE:
    {
      int dummy_err;
      char *end_not_used;
      return my_strntod(str_value.charset(), (char*) str_value.ptr(),
                        str_value.length(), &end_not_used, &dummy_err);
    }
  case TIME_VALUE:
    /*
      This works for example when user says SELECT ?+0.0 and supplies
      time value for the placeholder.
    */
    return ulonglong2double(TIME_to_ulonglong(&value.time));
  case NULL_VALUE:
    return 0.0;
  default:
    DBUG_ASSERT(0);
  }
  return 0.0;
} 


longlong Item_param::val_int() 
{ 
  switch (state) {
  case REAL_VALUE:
    return (longlong) (value.real + (value.real > 0 ? 0.5 : -0.5));
  case INT_VALUE:
    return value.integer;
  case STRING_VALUE:
  case LONG_DATA_VALUE:
    {
      int dummy_err;
      return my_strntoll(str_value.charset(), str_value.ptr(),
                         str_value.length(), 10, (char**) 0, &dummy_err);
    }
  case TIME_VALUE:
    return (longlong) TIME_to_ulonglong(&value.time);
  case NULL_VALUE:
    return 0; 
  default:
    DBUG_ASSERT(0);
  }
  return 0;
}


String *Item_param::val_str(String* str) 
{ 
  switch (state) {
  case STRING_VALUE:
  case LONG_DATA_VALUE:
    return &str_value_ptr;
  case REAL_VALUE:
    str->set(value.real, NOT_FIXED_DEC, &my_charset_bin);
    return str;
  case INT_VALUE:
    str->set(value.integer, &my_charset_bin);
    return str;
  case TIME_VALUE:
  {
    if (str->reserve(MAX_DATE_STRING_REP_LENGTH))
      break;
    str->length((uint) my_TIME_to_str(&value.time, (char*) str->ptr()));
    str->set_charset(&my_charset_bin);
    return str;
  }
  case NULL_VALUE:
    return NULL; 
  default:
    DBUG_ASSERT(0);
  }
  return str;
}

/*
  Return Param item values in string format, for generating the dynamic 
  query used in update/binary logs
  TODO: change interface and implementation to fill log data in place
  and avoid one more memcpy/alloc between str and log string.
*/

const String *Item_param::query_val_str(String* str) const
{
  switch (state) {
  case INT_VALUE:
    str->set(value.integer, &my_charset_bin);
    break;
  case REAL_VALUE:
    str->set(value.real, NOT_FIXED_DEC, &my_charset_bin);
    break;
  case TIME_VALUE:
    {
      char *buf, *ptr;
      str->length(0);
      /*
        TODO: in case of error we need to notify replication
        that binary log contains wrong statement 
      */
      if (str->reserve(MAX_DATE_STRING_REP_LENGTH+3))
        break; 

      /* Create date string inplace */
      buf= str->c_ptr_quick();
      ptr= buf;
      *ptr++= '\'';
      ptr+= (uint) my_TIME_to_str(&value.time, ptr);
      *ptr++= '\'';
      str->length((uint32) (ptr - buf));
      break;
    }
  case STRING_VALUE:
  case LONG_DATA_VALUE:
    {
      char *buf, *ptr;
      str->length(0);
      if (str->reserve(str_value.length()*2+3))
        break;

      buf= str->c_ptr_quick();
      ptr= buf;
      if (value.cs_info.character_set_client->escape_with_backslash_is_dangerous)
      {
        ptr= str_to_hex(ptr, str_value.ptr(), str_value.length());
      }
      else
      {
        *ptr++= '\'';
        ptr+= escape_string_for_mysql(str_value.charset(), ptr,
                                      str_value.ptr(), str_value.length());
        *ptr++='\'';
      }
      str->length(ptr - buf);
      break;
    }
  case NULL_VALUE:
    return &my_null_string;
  default:
    DBUG_ASSERT(0);
  }
  return str;
}


/*
  Convert string from client character set to the character set of
  connection.
*/

bool Item_param::convert_str_value(THD *thd)
{
  bool rc= FALSE;
  if (state == STRING_VALUE || state == LONG_DATA_VALUE)
  {
    /*
      Check is so simple because all charsets were set up properly
      in setup_one_conversion_function, where typecode of
      placeholder was also taken into account: the variables are different
      here only if conversion is really necessary.
    */
    if (value.cs_info.final_character_set_of_str_value !=
        value.cs_info.character_set_of_placeholder)
    {
      rc= thd->convert_string(&str_value,
                              value.cs_info.character_set_of_placeholder,
                              value.cs_info.final_character_set_of_str_value);
    }
    else
      str_value.set_charset(value.cs_info.final_character_set_of_str_value);
    /* Here str_value is guaranteed to be in final_character_set_of_str_value */

    max_length= str_value.length();
    decimals= 0;
    /*
      str_value_ptr is returned from val_str(). It must be not alloced
      to prevent it's modification by val_str() invoker.
    */
    str_value_ptr.set(str_value.ptr(), str_value.length(),
                      str_value.charset());
    /* Synchronize item charset with value charset */
    collation.set(str_value.charset(), DERIVATION_COERCIBLE);
  }
  return rc;
}


bool Item_param::basic_const_item() const
{
  if (state == NO_VALUE || state == TIME_VALUE)
    return FALSE;
  return TRUE;
}

Item *
Item_param::new_item()
{
  /* see comments in the header file */
  switch (state) {
  case NULL_VALUE:
    return new Item_null(name);
  case INT_VALUE:
    return (unsigned_flag ?
            new Item_uint(name, value.integer, max_length) :
            new Item_int(name, value.integer, max_length));
  case REAL_VALUE:
    return new Item_real(name, value.real, decimals, max_length);
  case STRING_VALUE:
  case LONG_DATA_VALUE:
    return new Item_string(name, str_value.c_ptr_quick(), str_value.length(),
                           str_value.charset());
  case TIME_VALUE:
    break;
  case NO_VALUE:
  default:
    DBUG_ASSERT(0);
  };
  return 0;
}


bool
Item_param::eq(const Item *arg, bool binary_cmp) const
{
  Item *item;
  if (!basic_const_item() || !arg->basic_const_item() || arg->type() != type())
    return FALSE;
  /*
    We need to cast off const to call val_int(). This should be OK for
    a basic constant.
  */
  item= (Item*) arg;

  switch (state) {
  case NULL_VALUE:
    return TRUE;
  case INT_VALUE:
    return value.integer == item->val_int() &&
           unsigned_flag == item->unsigned_flag;
  case REAL_VALUE:
    return value.real == item->val();
  case STRING_VALUE:
  case LONG_DATA_VALUE:
    if (binary_cmp)
      return !stringcmp(&str_value, &item->str_value);
    return !sortcmp(&str_value, &item->str_value, collation.collation);
  default:
    break;
  }
  return FALSE;
}

/* End of Item_param related */


void Item_copy_string::copy()
{
  String *res=item->val_str(&str_value);
  if (res && res != &str_value)
    str_value.copy(*res);
  null_value=item->null_value;
}

/* ARGSUSED */
String *Item_copy_string::val_str(String *str)
{
  // Item_copy_string is used without fix_fields call
  if (null_value)
    return (String*) 0;
  return &str_value;
}


int Item_copy_string::save_in_field(Field *field, bool no_conversions)
{
  if (null_value)
    return set_field_to_null(field);
  field->set_notnull();
  return field->store(str_value.ptr(),str_value.length(),
		      collation.collation);
}

/*
  Functions to convert item to field (for send_fields)
*/

/* ARGSUSED */
bool Item::fix_fields(THD *thd,
		      struct st_table_list *list,
		      Item ** ref)
{

  // We do not check fields which are fixed during construction
  DBUG_ASSERT(fixed == 0 || basic_const_item());
  fixed= 1;
  return 0;
}

double Item_ref_null_helper::val()
{
  DBUG_ASSERT(fixed == 1);
  double tmp= (*ref)->val_result();
  owner->was_null|= null_value= (*ref)->null_value;
  return tmp;
}


longlong Item_ref_null_helper::val_int()
{
  DBUG_ASSERT(fixed == 1);
  longlong tmp= (*ref)->val_int_result();
  owner->was_null|= null_value= (*ref)->null_value;
  return tmp;
}


String* Item_ref_null_helper::val_str(String* s)
{
  DBUG_ASSERT(fixed == 1);
  String* tmp= (*ref)->str_result(s);
  owner->was_null|= null_value= (*ref)->null_value;
  return tmp;
}


bool Item_ref_null_helper::get_date(TIME *ltime, uint fuzzydate)
{  
  return (owner->was_null|= null_value= (*ref)->get_date(ltime, fuzzydate));
}


/*
  Mark item and SELECT_LEXs as dependent if it is not outer resolving

  SYNOPSIS
    mark_as_dependent()
    thd - thread handler
    last - select from which current item depend
    current  - current select
    item - item which should be marked
*/

static void mark_as_dependent(THD *thd, SELECT_LEX *last, SELECT_LEX *current,
			      Item_ident *item)
{
  // store pointer on SELECT_LEX from which item is dependent
  item->depended_from= last;
  current->mark_as_dependent(last);
  if (thd->lex->describe & DESCRIBE_EXTENDED)
  {
    char warn_buff[MYSQL_ERRMSG_SIZE];
    sprintf(warn_buff, ER(ER_WARN_FIELD_RESOLVED),
	    (item->db_name?item->db_name:""), (item->db_name?".":""),
	    (item->table_name?item->table_name:""), (item->table_name?".":""),
	    item->field_name,
	    current->select_number, last->select_number);
    push_warning(thd, MYSQL_ERROR::WARN_LEVEL_NOTE,
		 ER_WARN_FIELD_RESOLVED, warn_buff);
  }
}


bool Item_field::fix_fields(THD *thd, TABLE_LIST *tables, Item **ref)
{
  enum_parsing_place place= NO_MATTER;
  DBUG_ASSERT(fixed == 0);
  if (!field)					// If field is not checked
  {
    TABLE_LIST *where= 0;
    bool upward_lookup= 0;
    Field *tmp= (Field *)not_found_field;
    if ((tmp= find_field_in_tables(thd, this, tables, &where, 0)) ==
	not_found_field)
    {
      /* Look up in current select's item_list to find aliased fields */
      if (thd->lex->current_select->is_item_list_lookup)
      {
        uint counter;
        bool not_used;
        Item** res= find_item_in_list(this, thd->lex->current_select->item_list,
                                      &counter, REPORT_EXCEPT_NOT_FOUND,
                                      &not_used);
        if (res != (Item **)not_found_item && (*res)->type() == Item::FIELD_ITEM)
        {
          set_field((*((Item_field**)res))->field);
          return 0;
        }
      }

      /*
	We can't find table field in table list of current select,
	consequently we have to find it in outer subselect(s).
	We can't join lists of outer & current select, because of scope
	of view rules. For example if both tables (outer & current) have
	field 'field' it is not mistake to refer to this field without
	mention of table name, but if we join tables in one list it will
	cause error ER_NON_UNIQ_ERROR in find_field_in_tables.
      */
      SELECT_LEX *last= 0;
#ifdef EMBEDDED_LIBRARY
      thd->net.last_errno= 0;
#endif
      TABLE_LIST *table_list;
      Item **refer= (Item **)not_found_item;
      uint counter;
      bool not_used;
      // Prevent using outer fields in subselects, that is not supported now
      SELECT_LEX *cursel= (SELECT_LEX *) thd->lex->current_select;
      if (cursel->master_unit()->first_select()->linkage != DERIVED_TABLE_TYPE)
      {
	SELECT_LEX_UNIT *prev_unit= cursel->master_unit();
	for (SELECT_LEX *sl= prev_unit->outer_select();
	     sl;
	     sl= (prev_unit= sl->master_unit())->outer_select())
	{
	  upward_lookup= 1;
	  table_list= (last= sl)->get_table_list();
	  if (sl->resolve_mode == SELECT_LEX::INSERT_MODE && table_list)
	  {
            /*
              it is primary INSERT st_select_lex => skip first table
              resolving
            */
	    table_list= table_list->next;
	  }

	  Item_subselect *prev_subselect_item= prev_unit->item;
          place= prev_subselect_item->parsing_place;
          /*
            check table fields only if subquery used somewhere out of HAVING
            or outer SELECT do not use groupping (i.e. tables are
            accessable)
          */
          if ((place != IN_HAVING ||
               (sl->with_sum_func == 0 && sl->group_list.elements == 0)) &&
              (tmp= find_field_in_tables(thd, this,
                                         table_list, &where,
                                         0)) != not_found_field)
          {
            if (!tmp)
              return -1;
            prev_subselect_item->used_tables_cache|= tmp->table->map;
            prev_subselect_item->const_item_cache= 0;
            break;
          }
	  if (sl->resolve_mode == SELECT_LEX::SELECT_MODE &&
	      (refer= find_item_in_list(this, sl->item_list, &counter,
                                        REPORT_EXCEPT_NOT_FOUND,
                                        &not_used)) !=
	       (Item **) not_found_item)
	  {
	    if (refer && (*refer)->fixed) // Avoid crash in case of error
	    {
	      prev_subselect_item->used_tables_cache|= (*refer)->used_tables();
	      prev_subselect_item->const_item_cache&= (*refer)->const_item();
	    }
	    break;
	  }

	  // Reference is not found => depend from outer (or just error)
	  prev_subselect_item->used_tables_cache|= OUTER_REF_TABLE_BIT;
	  prev_subselect_item->const_item_cache= 0;

	  if (sl->master_unit()->first_select()->linkage ==
	      DERIVED_TABLE_TYPE)
	    break; // do not look over derived table
	}
      }
      if (!tmp)
	return -1;
      else if (!refer)
	return 1;
      else if (tmp == not_found_field && refer == (Item **)not_found_item)
      {
	if (upward_lookup)
	{
	  // We can't say exactly what absend table or field
	  my_printf_error(ER_BAD_FIELD_ERROR, ER(ER_BAD_FIELD_ERROR), MYF(0),
			  full_name(), thd->where);
	}
	else
	{
	  // Call to report error
	  find_field_in_tables(thd, this, tables, &where, 1);
	}
	return -1;
      }
      else if (refer != (Item **)not_found_item)
      {
	if (!last->ref_pointer_array[counter])
	{
	  my_error(ER_ILLEGAL_REFERENCE, MYF(0), name,
		   "forward reference in item list");
	  return -1;
	}
        DBUG_ASSERT((*refer)->fixed);
        /*
          Here, a subset of actions performed by Item_ref::set_properties
          is not enough. So we pass ptr to NULL into Item_[direct]_ref
          constructor, so no initialization is performed, and call 
          fix_fields() below.
        */
        Item *save= last->ref_pointer_array[counter];
        last->ref_pointer_array[counter]= NULL;
	Item_ref *rf= (place == IN_HAVING ?
                       new Item_ref(last->ref_pointer_array + counter,
                                    (char *)table_name,
                                    (char *)field_name) :
                       new Item_direct_ref(last->ref_pointer_array + counter,
                                           (char *)table_name,
                                           (char *)field_name));
	if (!rf)
	  return 1;
        thd->change_item_tree(ref, rf);
        last->ref_pointer_array[counter]= save;
	/*
	  rf is Item_ref => never substitute other items (in this case)
	  during fix_fields() => we can use rf after fix_fields()
	*/
	if (rf->fix_fields(thd, tables, ref) || rf->check_cols(1))
	  return 1;

	mark_as_dependent(thd, last, cursel, rf);
	return 0;
      }
      else
      {
	mark_as_dependent(thd, last, cursel, this);
	if (last->having_fix_field)
	{
	  Item_ref *rf;
          rf= new Item_ref((where->db[0] ? where->db : 0),
                           (char*) where->alias, (char*) field_name);
	  if (!rf)
	    return 1;
          thd->change_item_tree(ref, rf);
	  /*
	    rf is Item_ref => never substitute other items (in this case)
	    during fix_fields() => we can use rf after fix_fields()
	  */
	  return rf->fix_fields(thd, tables, ref) ||  rf->check_cols(1);
	}
      }
    }
    else if (!tmp)
      return -1;

    set_field(tmp);
  }
  else if (thd->set_query_id && field->query_id != thd->query_id)
  {
    /* We only come here in unions */
    TABLE *table=field->table;
    field->query_id=thd->query_id;
    table->used_fields++;
    table->used_keys.intersect(field->part_of_key);
    fixed= 1;
  }
  return 0;
}

void Item_field::cleanup()
{
  DBUG_ENTER("Item_field::cleanup");
  Item_ident::cleanup();
  /*
    Even if this object was created by direct link to field in setup_wild()
    it will be linked correctly next tyme by name of field and table alias.
    I.e. we can drop 'field'.
   */
  field= result_field= 0;
  DBUG_VOID_RETURN;
}

void Item::init_make_field(Send_field *tmp_field,
			   enum enum_field_types field_type)
{
  char *empty_name= (char*) "";
  tmp_field->db_name=		empty_name;
  tmp_field->org_table_name=	empty_name;
  tmp_field->org_col_name=	empty_name;
  tmp_field->table_name=	empty_name;
  tmp_field->col_name=		name;
  tmp_field->charsetnr=         collation.collation->number;
  tmp_field->flags=             (maybe_null ? 0 : NOT_NULL_FLAG) | 
                                (my_binary_compare(collation.collation) ?
                                 BINARY_FLAG : 0);
  tmp_field->type=field_type;
  tmp_field->length=max_length;
  tmp_field->decimals=decimals;
  if (unsigned_flag)
    tmp_field->flags |= UNSIGNED_FLAG;
}

void Item::make_field(Send_field *tmp_field)
{
  init_make_field(tmp_field, field_type());
}


void Item_empty_string::make_field(Send_field *tmp_field)
{
  init_make_field(tmp_field,FIELD_TYPE_VAR_STRING);
}


enum_field_types Item::field_type() const
{
  return ((result_type() == STRING_RESULT) ? FIELD_TYPE_VAR_STRING :
	  (result_type() == INT_RESULT) ? FIELD_TYPE_LONGLONG :
	  FIELD_TYPE_DOUBLE);
}


Field *Item::tmp_table_field_from_field_type(TABLE *table)
{
  /*
    The field functions defines a field to be not null if null_ptr is not 0
  */
  uchar *null_ptr= maybe_null ? (uchar*) "" : 0;

  switch (field_type()) {
  case MYSQL_TYPE_DECIMAL:
    return new Field_decimal((char*) 0, max_length, null_ptr, 0, Field::NONE,
			     name, table, decimals, 0, unsigned_flag);
  case MYSQL_TYPE_TINY:
    return new Field_tiny((char*) 0, max_length, null_ptr, 0, Field::NONE,
			  name, table, 0, unsigned_flag);
  case MYSQL_TYPE_SHORT:
    return new Field_short((char*) 0, max_length, null_ptr, 0, Field::NONE,
			   name, table, 0, unsigned_flag);
  case MYSQL_TYPE_LONG:
    return new Field_long((char*) 0, max_length, null_ptr, 0, Field::NONE,
			  name, table, 0, unsigned_flag);
#ifdef HAVE_LONG_LONG
  case MYSQL_TYPE_LONGLONG:
    return new Field_longlong((char*) 0, max_length, null_ptr, 0, Field::NONE,
			      name, table, 0, unsigned_flag);
#endif
  case MYSQL_TYPE_FLOAT:
    return new Field_float((char*) 0, max_length, null_ptr, 0, Field::NONE,
			   name, table, decimals, 0, unsigned_flag);
  case MYSQL_TYPE_DOUBLE:
    return new Field_double((char*) 0, max_length, null_ptr, 0, Field::NONE,
			    name, table, decimals, 0, unsigned_flag);
  case MYSQL_TYPE_NULL:
    return new Field_null((char*) 0, max_length, Field::NONE,
			  name, table, &my_charset_bin);
  case MYSQL_TYPE_INT24:
    return new Field_medium((char*) 0, max_length, null_ptr, 0, Field::NONE,
			    name, table, 0, unsigned_flag);
  case MYSQL_TYPE_NEWDATE:
  case MYSQL_TYPE_DATE:
    return new Field_date(maybe_null, name, table, &my_charset_bin);
  case MYSQL_TYPE_TIME:
    return new Field_time(maybe_null, name, table, &my_charset_bin);
  case MYSQL_TYPE_TIMESTAMP:
  case MYSQL_TYPE_DATETIME:
    return new Field_datetime(maybe_null, name, table, &my_charset_bin);
  case MYSQL_TYPE_YEAR:
    return new Field_year((char*) 0, max_length, null_ptr, 0, Field::NONE,
			  name, table);
  default:
    /* This case should never be choosen */
    DBUG_ASSERT(0);
    /* If something goes awfully wrong, it's better to get a string than die */
  case MYSQL_TYPE_ENUM:
  case MYSQL_TYPE_SET:
  case MYSQL_TYPE_VAR_STRING:
    DBUG_ASSERT(collation.collation);
    if (max_length/collation.collation->mbmaxlen > 255)
      break;					// If blob
    return new Field_varstring(max_length, maybe_null, name, table,
			       collation.collation);
  case MYSQL_TYPE_STRING:
    DBUG_ASSERT(collation.collation);
    if (max_length/collation.collation->mbmaxlen > 255)		// If blob
      break;
    return new Field_string(max_length, maybe_null, name, table,
			    collation.collation);
  case MYSQL_TYPE_TINY_BLOB:
  case MYSQL_TYPE_MEDIUM_BLOB:
  case MYSQL_TYPE_LONG_BLOB:
  case MYSQL_TYPE_BLOB:
  case MYSQL_TYPE_GEOMETRY:
    break;					// Blob handled outside of case
  }

  /* blob is special as it's generated for both blobs and long strings */
  return new Field_blob(max_length, maybe_null, name, table,
			collation.collation);
}


/* ARGSUSED */
void Item_field::make_field(Send_field *tmp_field)
{
  field->make_field(tmp_field);
  DBUG_ASSERT(tmp_field->table_name);
  if (name)
    tmp_field->col_name=name;			// Use user supplied name
}


/*
  Set a field:s value from a item
*/

void Item_field::save_org_in_field(Field *to)
{
  if (field->is_null())
  {
    null_value=1;
    set_field_to_null_with_conversions(to, 1);
  }
  else
  {
    to->set_notnull();
    field_conv(to,field);
    null_value=0;
  }
}

int Item_field::save_in_field(Field *to, bool no_conversions)
{
  if (result_field->is_null())
  {
    null_value=1;
    return set_field_to_null_with_conversions(to, no_conversions);
  }
  else
  {
    to->set_notnull();
    field_conv(to,result_field);
    null_value=0;
  }
  return 0;
}


/*
  Store null in field

  SYNOPSIS
    save_in_field()
    field		Field where we want to store NULL

  DESCRIPTION
    This is used on INSERT.
    Allow NULL to be inserted in timestamp and auto_increment values

  RETURN VALUES
    0	 ok
    1	 Field doesn't support NULL values and can't handle 'field = NULL'
*/   

int Item_null::save_in_field(Field *field, bool no_conversions)
{
  return set_field_to_null_with_conversions(field, no_conversions);
}


/*
  Store null in field

  SYNOPSIS
    save_safe_in_field()
    field		Field where we want to store NULL

  RETURN VALUES
    0	 ok
    1	 Field doesn't support NULL values
*/   

int Item_null::save_safe_in_field(Field *field)
{
  return set_field_to_null(field);
}


int Item::save_in_field(Field *field, bool no_conversions)
{
  int error;
  if (result_type() == STRING_RESULT ||
      result_type() == REAL_RESULT &&
      field->result_type() == STRING_RESULT)
  {
    String *result;
    CHARSET_INFO *cs= collation.collation;
    char buff[MAX_FIELD_WIDTH];		// Alloc buffer for small columns
    str_value.set_quick(buff, sizeof(buff), cs);
    result=val_str(&str_value);
    if (null_value)
    {
      str_value.set_quick(0, 0, cs);
      return set_field_to_null_with_conversions(field, no_conversions);
    }
    field->set_notnull();
    error=field->store(result->ptr(),result->length(),cs);
    str_value.set_quick(0, 0, cs);
  }
  else if (result_type() == REAL_RESULT)
  {
    double nr=val();
    if (null_value)
      return set_field_to_null(field);
    field->set_notnull();
    error=field->store(nr);
  }
  else
  {
    longlong nr=val_int();
    if (null_value)
      return set_field_to_null_with_conversions(field, no_conversions);
    field->set_notnull();
    error=field->store(nr);
  }
  return error;
}


int Item_string::save_in_field(Field *field, bool no_conversions)
{
  String *result;
  result=val_str(&str_value);
  if (null_value)
    return set_field_to_null(field);
  field->set_notnull();
  return field->store(result->ptr(),result->length(),collation.collation);
}

int Item_uint::save_in_field(Field *field, bool no_conversions)
{
  /*
    TODO: To be fixed when wen have a
    field->store(longlong, unsigned_flag) method 
  */
  return Item_int::save_in_field(field, no_conversions);
}


int Item_int::save_in_field(Field *field, bool no_conversions)
{
  longlong nr=val_int();
  if (null_value)
    return set_field_to_null(field);
  field->set_notnull();
  return field->store(nr);
}


bool Item_int::eq(const Item *arg, bool binary_cmp) const
{
  /* No need to check for null value as basic constant can't be NULL */
  if (arg->basic_const_item() && arg->type() == type())
  {
    /*
      We need to cast off const to call val_int(). This should be OK for
      a basic constant.
    */
    Item *item= (Item*) arg;
    return item->val_int() == value && item->unsigned_flag == unsigned_flag;
  }
  return FALSE;
}


Item *Item_int_with_ref::new_item()
{
  DBUG_ASSERT(ref->const_item());
  /*
    We need to evaluate the constant to make sure it works with
    parameter markers.
  */
  return (ref->unsigned_flag ?
          new Item_uint(ref->name, ref->val_int(), ref->max_length) :
          new Item_int(ref->name, ref->val_int(), ref->max_length));
}


Item_num *Item_uint::neg()
{
  return new Item_real(name, - ((double) value), 0, max_length);
}

int Item_real::save_in_field(Field *field, bool no_conversions)
{
  double nr=val();
  if (null_value)
    return set_field_to_null(field);
  field->set_notnull();
  return field->store(nr);
}


bool Item_real::eq(const Item *arg, bool binary_cmp) const
{
  if (arg->basic_const_item() && arg->type() == type())
  {
    /*
      We need to cast off const to call val_int(). This should be OK for
      a basic constant.
    */
    Item *item= (Item*) arg;
    return item->val() == value;
  }
  return FALSE;
}

/****************************************************************************
** varbinary item
** In string context this is a binary string
** In number context this is a longlong value.
****************************************************************************/

inline uint char_val(char X)
{
  return (uint) (X >= '0' && X <= '9' ? X-'0' :
		 X >= 'A' && X <= 'Z' ? X-'A'+10 :
		 X-'a'+10);
}


Item_varbinary::Item_varbinary(const char *str, uint str_length)
{
  name=(char*) str-2;				// Lex makes this start with 0x
  max_length=(str_length+1)/2;
  char *ptr=(char*) sql_alloc(max_length+1);
  if (!ptr)
    return;
  str_value.set(ptr,max_length,&my_charset_bin);
  char *end=ptr+max_length;
  if (max_length*2 != str_length)
    *ptr++=char_val(*str++);			// Not even, assume 0 prefix
  while (ptr != end)
  {
    *ptr++= (char) (char_val(str[0])*16+char_val(str[1]));
    str+=2;
  }
  *ptr=0;					// Keep purify happy
  collation.set(&my_charset_bin, DERIVATION_COERCIBLE);
  fixed= 1;
  unsigned_flag= 1;
}

longlong Item_varbinary::val_int()
{
  // following assert is redundant, because fixed=1 assigned in constructor
  DBUG_ASSERT(fixed == 1);
  char *end=(char*) str_value.ptr()+str_value.length(),
       *ptr=end-min(str_value.length(),sizeof(longlong));

  ulonglong value=0;
  for (; ptr != end ; ptr++)
    value=(value << 8)+ (ulonglong) (uchar) *ptr;
  return (longlong) value;
}


int Item_varbinary::save_in_field(Field *field, bool no_conversions)
{
  int error;
  field->set_notnull();
  if (field->result_type() == STRING_RESULT)
  {
    error=field->store(str_value.ptr(),str_value.length(),collation.collation);
  }
  else
  {
    longlong nr=val_int();
    error=field->store(nr);
  }
  return error;
}


bool Item_varbinary::eq(const Item *arg, bool binary_cmp) const
{
  if (arg->basic_const_item() && arg->type() == type())
  {
    if (binary_cmp)
      return !stringcmp(&str_value, &arg->str_value);
    return !sortcmp(&str_value, &arg->str_value, collation.collation);
  }
  return FALSE;
}


Item *Item_varbinary::safe_charset_converter(CHARSET_INFO *tocs)
{
  Item_string *conv;
  String tmp, *str= val_str(&tmp);

  if (!(conv= new Item_string(str->ptr(), str->length(), tocs)))
    return NULL;
  conv->str_value.copy();
  conv->str_value.shrink_to_length();
  return conv;
}


/*
  Pack data in buffer for sending
*/

bool Item_null::send(Protocol *protocol, String *packet)
{
  return protocol->store_null();
}

/*
  This is only called from items that is not of type item_field
*/

bool Item::send(Protocol *protocol, String *buffer)
{
  bool result;
  enum_field_types type;
  LINT_INIT(result);

  switch ((type=field_type())) {
  default:
  case MYSQL_TYPE_NULL:
  case MYSQL_TYPE_DECIMAL:
  case MYSQL_TYPE_ENUM:
  case MYSQL_TYPE_SET:
  case MYSQL_TYPE_TINY_BLOB:
  case MYSQL_TYPE_MEDIUM_BLOB:
  case MYSQL_TYPE_LONG_BLOB:
  case MYSQL_TYPE_BLOB:
  case MYSQL_TYPE_GEOMETRY:
  case MYSQL_TYPE_STRING:
  case MYSQL_TYPE_VAR_STRING:
  {
    String *res;
    if ((res=val_str(buffer)))
      result= protocol->store(res->ptr(),res->length(),res->charset());
    break;
  }
  case MYSQL_TYPE_TINY:
  {
    longlong nr;
    nr= val_int();
    if (!null_value)
      result= protocol->store_tiny(nr);
    break;
  }
  case MYSQL_TYPE_SHORT:
  {
    longlong nr;
    nr= val_int();
    if (!null_value)
      result= protocol->store_short(nr);
    break;
  }
  case MYSQL_TYPE_INT24:
  case MYSQL_TYPE_LONG:
  {
    longlong nr;
    nr= val_int();
    if (!null_value)
      result= protocol->store_long(nr);
    break;
  }
  case MYSQL_TYPE_LONGLONG:
  {
    longlong nr;
    nr= val_int();
    if (!null_value)
      result= protocol->store_longlong(nr, unsigned_flag);
    break;
  }
  case MYSQL_TYPE_FLOAT:
  {
    float nr;
    nr= (float) val();
    if (!null_value)
      result= protocol->store(nr, decimals, buffer);
    break;
  }
  case MYSQL_TYPE_DOUBLE:
  {
    double nr;
    nr= val();
    if (!null_value)
      result= protocol->store(nr, decimals, buffer);
    break;
  }
  case MYSQL_TYPE_DATETIME:
  case MYSQL_TYPE_DATE:
  case MYSQL_TYPE_TIMESTAMP:
  {
    TIME tm;
    get_date(&tm, TIME_FUZZY_DATE);
    if (!null_value)
    {
      if (type == MYSQL_TYPE_DATE)
	return protocol->store_date(&tm);
      else
	result= protocol->store(&tm);
    }
    break;
  }
  case MYSQL_TYPE_TIME:
  {
    TIME tm;
    get_time(&tm);
    if (!null_value)
      result= protocol->store_time(&tm);
    break;
  }
  }
  if (null_value)
    result= protocol->store_null();
  return result;
}


bool Item_field::send(Protocol *protocol, String *buffer)
{
  return protocol->store(result_field);
}


/*
  This is used for HAVING clause
  Find field in select list having the same name
*/

bool Item_ref::fix_fields(THD *thd,TABLE_LIST *tables, Item **reference)
{
  DBUG_ASSERT(fixed == 0);
  uint counter;
  enum_parsing_place place= NO_MATTER;
  bool not_used;
  if (!ref)
  {
    TABLE_LIST *where= 0, *table_list;
    SELECT_LEX_UNIT *prev_unit= thd->lex->current_select->master_unit();
    SELECT_LEX *sl= prev_unit->outer_select();
    /*
      Finding only in current select will be performed for selects that have 
      not outer one and for derived tables (which not support using outer 
      fields for now)
    */
    if ((ref= find_item_in_list(this, 
				*(thd->lex->current_select->get_item_list()),
				&counter,
				((sl && 
				  thd->lex->current_select->master_unit()->
				  first_select()->linkage !=
				  DERIVED_TABLE_TYPE) ? 
				  REPORT_EXCEPT_NOT_FOUND :
				  REPORT_ALL_ERRORS ), &not_used)) ==
	(Item **)not_found_item)
    {
      Field *tmp= (Field*) not_found_field;
      SELECT_LEX *last= 0;
      /*
	We can't find table field in select list of current select,
	consequently we have to find it in outer subselect(s).
	We can't join lists of outer & current select, because of scope
	of view rules. For example if both tables (outer & current) have
	field 'field' it is not mistake to refer to this field without
	mention of table name, but if we join tables in one list it will
	cause error ER_NON_UNIQ_ERROR in find_item_in_list.
      */
      for ( ; sl ; sl= (prev_unit= sl->master_unit())->outer_select())
      {
	last= sl;
	Item_subselect *prev_subselect_item= prev_unit->item;
	if (sl->resolve_mode == SELECT_LEX::SELECT_MODE &&
	    (ref= find_item_in_list(this, sl->item_list,
                                    &counter, REPORT_EXCEPT_NOT_FOUND,
                                    &not_used)) !=
	   (Item **)not_found_item)
	{
	  if (ref && (*ref)->fixed) // Avoid crash in case of error
	  {
	    prev_subselect_item->used_tables_cache|= (*ref)->used_tables();
	    prev_subselect_item->const_item_cache&= (*ref)->const_item();
	  }
	  break;
	}
	table_list= sl->get_table_list();
	if (sl->resolve_mode == SELECT_LEX::INSERT_MODE && table_list)
	{
	  // it is primary INSERT st_select_lex => skip first table resolving
	  table_list= table_list->next;
	}
        place= prev_subselect_item->parsing_place;
        /*
          check table fields only if subquery used somewhere out of HAVING
          or SELECT list or outer SELECT do not use groupping (i.e. tables
          are accessable)
        */
        if ((place != IN_HAVING ||
             (sl->with_sum_func == 0 && sl->group_list.elements == 0)) &&
            (tmp= find_field_in_tables(thd, this,
                                       table_list, &where,
                                       0)) != not_found_field)
        {
          prev_subselect_item->used_tables_cache|= tmp->table->map;
          prev_subselect_item->const_item_cache= 0;
          break;
        }
        // Reference is not found => depend from outer (or just error)
	prev_subselect_item->used_tables_cache|= OUTER_REF_TABLE_BIT;
	prev_subselect_item->const_item_cache= 0;

	if (sl->master_unit()->first_select()->linkage ==
	    DERIVED_TABLE_TYPE)
	  break; // do not look over derived table
      }

      if (!ref)
	return 1;
      if (!tmp)
	return -1;
      if (ref == (Item **)not_found_item && tmp == not_found_field)
      {
        // We can't say exactly what absend (table or field)
        my_printf_error(ER_BAD_FIELD_ERROR, ER(ER_BAD_FIELD_ERROR), MYF(0),
                        full_name(), thd->where);
	ref= 0;                                 // Safety
	return 1;
      }
      if (tmp != not_found_field)
      {
        Item_field* fld;
        /*
          Set ref to 0 as we are replacing this item with the found item
          and this will ensure we get an error if this item would be
          used elsewhere
        */
	ref= 0;                                 // Safety
	if (!(fld= new Item_field(tmp)))
	  return 1;
	thd->change_item_tree(reference, fld);
	mark_as_dependent(thd, last, thd->lex->current_select, fld);
	return 0;
      }
      if (!last->ref_pointer_array[counter])
      {
        my_error(ER_ILLEGAL_REFERENCE, MYF(0), name,
                 "forward reference in item list");
        return -1;
      }
      DBUG_ASSERT((*ref)->fixed);
      mark_as_dependent(thd, last, thd->lex->current_select,
                        this);
      if (place == IN_HAVING)
      {
        Item_ref *rf;
        if (!(rf= new Item_direct_ref(last->ref_pointer_array + counter,
                                      (char *)table_name,
                                      (char *)field_name)))
          return 1;
        ref= 0;                                 // Safety
        if (rf->fix_fields(thd, tables, ref) || rf->check_cols(1))
	  return 1;
        thd->change_item_tree(reference, rf);
        return 0;
      }
      ref= last->ref_pointer_array + counter;
    }
    else if (!ref)
      return 1;
    else
    {
      if (!(*ref)->fixed)
      {
	my_error(ER_ILLEGAL_REFERENCE, MYF(0), name,
		 "forward reference in item list");
	return -1;
      }
      ref= thd->lex->current_select->ref_pointer_array + counter;
    }
  }

  /*
    The following conditional is changed as to correctly identify 
    incorrect references in group functions or forward references 
    with sub-select's / derived tables, while it prevents this 
    check when Item_ref is created in an expression involving 
    summing function, which is to be placed in the user variable.
  */
  if (((*ref)->with_sum_func && name &&
       (depended_from ||
	!(thd->lex->current_select->linkage != GLOBAL_OPTIONS_TYPE &&
	  thd->lex->current_select->having_fix_field))) ||
      !(*ref)->fixed)
  {
    my_error(ER_ILLEGAL_REFERENCE, MYF(0), name, 
	     ((*ref)->with_sum_func?
	      "reference on group function":
	      "forward reference in item list"));
    return 1;
  }

  set_properties();

  if (ref && (*ref)->check_cols(1))
    return 1;
  return 0;
}

void Item_ref::set_properties()
{
  max_length= (*ref)->max_length;
  maybe_null= (*ref)->maybe_null;
  decimals=   (*ref)->decimals;
  collation.set((*ref)->collation);
  with_sum_func= (*ref)->with_sum_func;
  unsigned_flag= (*ref)->unsigned_flag;
  fixed= 1;
}

void Item_ref::print(String *str)
{
  if (ref && *ref)
    (*ref)->print(str);
  else
    Item_ident::print(str);
}


void Item_ref_null_helper::print(String *str)
{
  str->append("<ref_null_helper>(", 18);
  if (ref && *ref)
    (*ref)->print(str);
  else
    str->append('?');
  str->append(')');
}


bool Item_default_value::eq(const Item *item, bool binary_cmp) const
{
  return item->type() == DEFAULT_VALUE_ITEM && 
    ((Item_default_value *)item)->arg->eq(arg, binary_cmp);
}


bool Item_default_value::fix_fields(THD *thd,
				    struct st_table_list *table_list,
				    Item **items)
{
  DBUG_ASSERT(fixed == 0);
  if (!arg)
  {
    fixed= 1;
    return 0;
  }
  if (!arg->fixed && arg->fix_fields(thd, table_list, &arg))
    return 1;
  
  if (arg->type() == REF_ITEM)
  {
    Item_ref *ref= (Item_ref *)arg;
    if (ref->ref[0]->type() != FIELD_ITEM)
    {
      return 1;
    }
    arg= ref->ref[0];
  }
  Item_field *field_arg= (Item_field *)arg;
  Field *def_field= (Field*) sql_alloc(field_arg->field->size_of());
  if (!def_field)
    return 1;
  memcpy(def_field, field_arg->field, field_arg->field->size_of());
  def_field->move_field(def_field->table->default_values -
                        def_field->table->record[0]);
  set_field(def_field);
  return 0;
}

void Item_default_value::print(String *str)
{
  if (!arg)
  {
    str->append("default", 7);
    return;
  }
  str->append("default(", 8);
  arg->print(str);
  str->append(')');
}

bool Item_insert_value::eq(const Item *item, bool binary_cmp) const
{
  return item->type() == INSERT_VALUE_ITEM &&
    ((Item_default_value *)item)->arg->eq(arg, binary_cmp);
}


bool Item_insert_value::fix_fields(THD *thd,
				   struct st_table_list *table_list,
				   Item **items)
{
  DBUG_ASSERT(fixed == 0);
  st_table_list *orig_next_table= table_list->next;
  table_list->next= 0;
  if (!arg->fixed && arg->fix_fields(thd, table_list, &arg))
  {
    table_list->next= orig_next_table;
    return 1;
  }
  table_list->next= orig_next_table;

  if (arg->type() == REF_ITEM)
  {
    Item_ref *ref= (Item_ref *)arg;
    if (ref->ref[0]->type() != FIELD_ITEM)
    {
      return 1;
    }
    arg= ref->ref[0];
  }
  Item_field *field_arg= (Item_field *)arg;

  if (field_arg->field->table->insert_values)
  {
    Field *def_field= (Field*) sql_alloc(field_arg->field->size_of());
    if (!def_field)
      return 1;
    memcpy(def_field, field_arg->field, field_arg->field->size_of());
    def_field->move_field(def_field->table->insert_values -
                          def_field->table->record[0]);
    set_field(def_field);
  }
  else
  {
    Field *tmp_field= field_arg->field;
    /* charset doesn't matter here, it's to avoid sigsegv only */
    set_field(new Field_null(0, 0, Field::NONE, tmp_field->field_name,
			     tmp_field->table, &my_charset_bin));
  }
  return 0;
}

void Item_insert_value::print(String *str)
{
  str->append("values(", 7);
  arg->print(str);
  str->append(')');
}

/*
  If item is a const function, calculate it and return a const item
  The original item is freed if not returned
*/

Item_result item_cmp_type(Item_result a,Item_result b)
{
  if (a == STRING_RESULT && b == STRING_RESULT)
    return STRING_RESULT;
  if (a == INT_RESULT && b == INT_RESULT)
    return INT_RESULT;
  else if (a == ROW_RESULT || b == ROW_RESULT)
    return ROW_RESULT;
  return REAL_RESULT;
}


void resolve_const_item(THD *thd, Item **ref, Item *comp_item)
{
  Item *item= *ref;
  Item *new_item= NULL;
  if (item->basic_const_item())
    return;                                     // Can't be better
  Item_result res_type=item_cmp_type(comp_item->result_type(),
				     item->result_type());
  char *name=item->name;			// Alloced by sql_alloc

  if (res_type == STRING_RESULT)
  {
    char buff[MAX_FIELD_WIDTH];
    String tmp(buff,sizeof(buff),&my_charset_bin),*result;
    result=item->val_str(&tmp);
    if (item->null_value)
      new_item= new Item_null(name);
    else
    {
      uint length= result->length();
      char *tmp_str= sql_strmake(result->ptr(), length);
      new_item= new Item_string(name, tmp_str, length, result->charset());
    }
  }
  else if (res_type == INT_RESULT)
  {
    longlong result=item->val_int();
    uint length=item->max_length;
    bool null_value=item->null_value;
    new_item= (null_value ? (Item*) new Item_null(name) :
               (Item*) new Item_int(name, result, length));
  }
  else if (res_type == ROW_RESULT && item->type() == Item::ROW_ITEM &&
           comp_item->type() == Item::ROW_ITEM)
  {
    /*
      Substitute constants only in Item_rows. Don't affect other Items
      with ROW_RESULT (eg Item_singlerow_subselect).

      For such Items more optimal is to detect if it is constant and replace
      it with Item_row. This would optimize queries like this:
      SELECT * FROM t1 WHERE (a,b) = (SELECT a,b FROM t2 LIMIT 1);
    */
    Item_row *item_row= (Item_row*) item;
    Item_row *comp_item_row= (Item_row*) comp_item;
    uint col;
    new_item= 0;
    /*
      If item and comp_item are both Item_rows and have same number of cols
      then process items in Item_row one by one.
      We can't ignore NULL values here as this item may be used with <=>, in
      which case NULL's are significant.
    */
    DBUG_ASSERT(item->result_type() == comp_item->result_type());
    DBUG_ASSERT(item_row->cols() == comp_item_row->cols());
    col= item_row->cols();
    while (col-- > 0)
      resolve_const_item(thd, item_row->addr(col), comp_item_row->el(col));
  }
  else if (res_type == REAL_RESULT)
  {						// It must REAL_RESULT
    double result=item->val();
    uint length=item->max_length,decimals=item->decimals;
    bool null_value=item->null_value;
    new_item= (null_value ? (Item*) new Item_null(name) : (Item*)
               new Item_real(name, result, decimals, length));
  }
  if (new_item)
    thd->change_item_tree(ref, new_item);
}

/*
  Return true if the value stored in the field is equal to the const item
  We need to use this on the range optimizer because in some cases
  we can't store the value in the field without some precision/character loss.
*/

bool field_is_equal_to_item(Field *field,Item *item)
{

  Item_result res_type=item_cmp_type(field->result_type(),
				     item->result_type());
  if (res_type == STRING_RESULT)
  {
    char item_buff[MAX_FIELD_WIDTH];
    char field_buff[MAX_FIELD_WIDTH];
    String item_tmp(item_buff,sizeof(item_buff),&my_charset_bin),*item_result;
    String field_tmp(field_buff,sizeof(field_buff),&my_charset_bin);
    item_result=item->val_str(&item_tmp);
    if (item->null_value)
      return 1;					// This must be true
    field->val_str(&field_tmp);
    return !stringcmp(&field_tmp,item_result);
  }
  if (res_type == INT_RESULT)
    return 1;					// Both where of type int
  double result=item->val();
  if (item->null_value)
    return 1;
  return result == field->val_real();
}

Item_cache* Item_cache::get_cache(Item_result type)
{
  switch (type)
  {
  case INT_RESULT:
    return new Item_cache_int();
  case REAL_RESULT:
    return new Item_cache_real();
  case STRING_RESULT:
    return new Item_cache_str();
  case ROW_RESULT:
    return new Item_cache_row();
  default:
    // should never be in real life
    DBUG_ASSERT(0);
    return 0;
  }
}


void Item_cache::print(String *str)
{
  str->append("<cache>(", 8);
  if (example)
    example->print(str);
  else
    Item::print(str);
  str->append(')');
}


void Item_cache_int::store(Item *item)
{
  value= item->val_int_result();
  null_value= item->null_value;
}


void Item_cache_real::store(Item *item)
{
  value= item->val_result();
  null_value= item->null_value;
}


void Item_cache_str::store(Item *item)
{
  value_buff.set(buffer, sizeof(buffer), item->collation.collation);
  value= item->str_result(&value_buff);
  if ((null_value= item->null_value))
    value= 0;
  else if (value != &value_buff)
  {
    /*
      We copy string value to avoid changing value if 'item' is table field
      in queries like following (where t1.c is varchar):
      select a, 
             (select a,b,c from t1 where t1.a=t2.a) = ROW(a,2,'a'),
             (select c from t1 where a=t2.a)
        from t2;
    */
    value_buff.copy(*value);
    value= &value_buff;
  }
}


double Item_cache_str::val()
{
  DBUG_ASSERT(fixed == 1);
  int err;
  if (value)
  {
    char *end_not_used;
    return my_strntod(value->charset(), (char*) value->ptr(),
		      value->length(), &end_not_used, &err);
  }
  return (double)0;
}


longlong Item_cache_str::val_int()
{
  DBUG_ASSERT(fixed == 1);
  int err;
  if (value)
    return my_strntoll(value->charset(), value->ptr(),
		       value->length(), 10, (char**) 0, &err);
  else
    return (longlong)0;
}


bool Item_cache_row::allocate(uint num)
{
  item_count= num;
  THD *thd= current_thd;
  return (!(values= 
	    (Item_cache **) thd->calloc(sizeof(Item_cache *)*item_count)));
}


bool Item_cache_row::setup(Item * item)
{
  example= item;
  if (!values && allocate(item->cols()))
    return 1;
  for (uint i= 0; i < item_count; i++)
  {
    Item *el= item->el(i);
    Item_cache *tmp;
    if (!(tmp= values[i]= Item_cache::get_cache(el->result_type())))
      return 1;
    tmp->setup(el);
  }
  return 0;
}


void Item_cache_row::store(Item * item)
{
  null_value= 0;
  item->bring_value();
  for (uint i= 0; i < item_count; i++)
  {
    values[i]->store(item->el(i));
    null_value|= values[i]->null_value;
  }
}


void Item_cache_row::illegal_method_call(const char *method)
{
  DBUG_ENTER("Item_cache_row::illegal_method_call");
  DBUG_PRINT("error", ("!!! %s method was called for row item", method));
  DBUG_ASSERT(0);
  my_error(ER_OPERAND_COLUMNS, MYF(0), 1);
  DBUG_VOID_RETURN;
}


bool Item_cache_row::check_cols(uint c)
{
  if (c != item_count)
  {
    my_error(ER_OPERAND_COLUMNS, MYF(0), c);
    return 1;
  }
  return 0;
}


bool Item_cache_row::null_inside()
{
  for (uint i= 0; i < item_count; i++)
  {
    if (values[i]->cols() > 1)
    {
      if (values[i]->null_inside())
	return 1;
    }
    else
    {
      values[i]->val_int();
      if (values[i]->null_value)
	return 1;
    }
  }
  return 0;
}


void Item_cache_row::bring_value()
{
  for (uint i= 0; i < item_count; i++)
    values[i]->bring_value();
  return;
}


Item_type_holder::Item_type_holder(THD *thd, Item *item)
  :Item(thd, item), enum_set_typelib(0), fld_type(get_real_type(item))
{
  DBUG_ASSERT(item->fixed);

  max_length= display_length(item);
  maybe_null= item->maybe_null;
  collation.set(item->collation);
  get_full_info(item);
}


/*
  Return expression type of Item_type_holder

  SYNOPSIS
    Item_type_holder::result_type()

  RETURN
     Item_result (type of internal MySQL expression result)
*/

Item_result Item_type_holder::result_type() const
{
  return Field::result_merge_type(fld_type);
}


/*
  Find real field type of item

  SYNOPSIS
    Item_type_holder::get_real_type()

  RETURN
    type of field which should be created to store item value
*/

enum_field_types Item_type_holder::get_real_type(Item *item)
{
  switch(item->type())
  {
  case FIELD_ITEM:
  {
    /*
      Item_fields::field_type ask Field_type() but sometimes field return
      a different type, like for enum/set, so we need to ask real type.
    */
    Field *field= ((Item_field *) item)->field;
    enum_field_types type= field->real_type();
    /* work around about varchar type field detection */
    if (type == MYSQL_TYPE_STRING && field->type() == MYSQL_TYPE_VAR_STRING)
      return MYSQL_TYPE_VAR_STRING;
    return type;
  }
  case SUM_FUNC_ITEM:
  {
    /*
      Argument of aggregate function sometimes should be asked about field
      type
    */
    Item_sum *item_sum= (Item_sum *) item;
    if (item_sum->keep_field_type())
      return get_real_type(item_sum->args[0]);
    break;
  }
  case FUNC_ITEM:
    if (((Item_func *) item)->functype() == Item_func::VAR_VALUE_FUNC)
    {
      /*
        There are work around of problem with changing variable type on the
        fly and variable always report "string" as field type to get
        acceptable information for client in send_field, so we make field
        type from expression type.
      */
      switch (item->result_type())
      {
      case STRING_RESULT:
        return MYSQL_TYPE_VAR_STRING;
      case INT_RESULT:
        return MYSQL_TYPE_LONGLONG;
      case REAL_RESULT:
        return MYSQL_TYPE_DOUBLE;
      case ROW_RESULT:
      default:
        DBUG_ASSERT(0);
        return MYSQL_TYPE_VAR_STRING;
      }
    }
    break;
  default:
    break;
  }
  return item->field_type();
}

/*
  Find field type which can carry current Item_type_holder type and
  type of given Item.

  SYNOPSIS
    Item_type_holder::join_types()
    thd     thread handler
    item    given item to join its parameters with this item ones

  RETURN
    TRUE   error - types are incompatible
    FALSE  OK
*/

bool Item_type_holder::join_types(THD *thd, Item *item)
{
  uint max_length_orig= max_length;
  uint decimals_orig= decimals;
  max_length= max(max_length, display_length(item));
  decimals= max(decimals, item->decimals);
  fld_type= Field::field_type_merge(fld_type, get_real_type(item));
  switch (Field::result_merge_type(fld_type))
  {
  case STRING_RESULT:
  {
    const char *old_cs, *old_derivation;
    old_cs= collation.collation->name;
    old_derivation= collation.derivation_name();
    if (collation.aggregate(item->collation, MY_COLL_ALLOW_CONV))
    {
      my_error(ER_CANT_AGGREGATE_2COLLATIONS, MYF(0),
	       old_cs, old_derivation,
	       item->collation.collation->name,
	       item->collation.derivation_name(),
	       "UNION");
      return TRUE;
    }
    break;
  }
  case REAL_RESULT:
  {
    if (decimals != NOT_FIXED_DEC)
    {
      int delta1= max_length_orig - decimals_orig;
      int delta2= item->max_length - item->decimals;
      if (fld_type == MYSQL_TYPE_DECIMAL)
        max_length= max(delta1, delta2) + decimals;
      else
        max_length= min(max(delta1, delta2) + decimals,
                        (fld_type == MYSQL_TYPE_FLOAT) ? FLT_DIG+6 : DBL_DIG+7);
    }
    else
      max_length= (fld_type == MYSQL_TYPE_FLOAT) ? FLT_DIG+6 : DBL_DIG+7;
    break;
  }
  default:;
  };
  maybe_null|= item->maybe_null;
  get_full_info(item);
  return FALSE;
}

/*
  Calculate lenth for merging result for given Item type

  SYNOPSIS
    Item_type_holder::real_length()
    item  Item for lrngth detection

  RETURN
    length
*/

uint32 Item_type_holder::display_length(Item *item)
{
  if (item->type() == Item::FIELD_ITEM)
    return ((Item_field *)item)->max_disp_length();

  switch (item->field_type())
  {
  case MYSQL_TYPE_DECIMAL:
  case MYSQL_TYPE_TIMESTAMP:
  case MYSQL_TYPE_DATE:
  case MYSQL_TYPE_TIME:
  case MYSQL_TYPE_DATETIME:
  case MYSQL_TYPE_YEAR:
  case MYSQL_TYPE_NEWDATE:
  case MYSQL_TYPE_ENUM:
  case MYSQL_TYPE_SET:
  case MYSQL_TYPE_TINY_BLOB:
  case MYSQL_TYPE_MEDIUM_BLOB:
  case MYSQL_TYPE_LONG_BLOB:
  case MYSQL_TYPE_BLOB:
  case MYSQL_TYPE_VAR_STRING:
  case MYSQL_TYPE_STRING:
  case MYSQL_TYPE_GEOMETRY:
    return item->max_length;
  case MYSQL_TYPE_TINY:
    return 4;
  case MYSQL_TYPE_SHORT:
    return 6;
  case MYSQL_TYPE_LONG:
    return 11;
  case MYSQL_TYPE_FLOAT:
    return 25;
  case MYSQL_TYPE_DOUBLE:
    return 53;
  case MYSQL_TYPE_NULL:
    return 4;
  case MYSQL_TYPE_LONGLONG:
    return 20;
  case MYSQL_TYPE_INT24:
    return 8;
  default:
    DBUG_ASSERT(0); // we should never go there
    return 0;
  }
}


/*
  Make temporary table field according collected information about type
  of UNION result

  SYNOPSIS
    Item_type_holder::make_field_by_type()
    table  temporary table for which we create fields

  RETURN
    created field
*/

Field *Item_type_holder::make_field_by_type(TABLE *table)
{
  /*
    The field functions defines a field to be not null if null_ptr is not 0
  */
  uchar *null_ptr= maybe_null ? (uchar*) "" : 0;
  switch (fld_type)
  {
  case MYSQL_TYPE_ENUM:
    DBUG_ASSERT(enum_set_typelib);
    return new Field_enum((char *) 0, max_length, null_ptr, 0,
                          Field::NONE, name,
                          table, get_enum_pack_length(enum_set_typelib->count),
                          enum_set_typelib, collation.collation);
  case MYSQL_TYPE_SET:
    DBUG_ASSERT(enum_set_typelib);
    return new Field_set((char *) 0, max_length, null_ptr, 0,
                         Field::NONE, name,
                         table, get_set_pack_length(enum_set_typelib->count),
                         enum_set_typelib, collation.collation);
  case MYSQL_TYPE_VAR_STRING:
    table->db_create_options|= HA_OPTION_PACK_RECORD;
    fld_type= MYSQL_TYPE_STRING;
    break;
  default:
    break;
  }
  return tmp_table_field_from_field_type(table);
}


/*
  Get full information from Item about enum/set fields to be able to create
  them later

  SYNOPSIS
    Item_type_holder::get_full_info
    item    Item for information collection
*/
void Item_type_holder::get_full_info(Item *item)
{
  if (fld_type == MYSQL_TYPE_ENUM ||
      fld_type == MYSQL_TYPE_SET)
  {
    if (item->type() == Item::SUM_FUNC_ITEM &&
        (((Item_sum*)item)->sum_func() == Item_sum::MAX_FUNC ||
         ((Item_sum*)item)->sum_func() == Item_sum::MIN_FUNC))
      item = ((Item_sum*)item)->args[0];
    /*
      We can have enum/set type after merging only if we have one enum|set
      field (or MIN|MAX(enum|set field)) and number of NULL fields
    */
    DBUG_ASSERT((enum_set_typelib &&
                 get_real_type(item) == MYSQL_TYPE_NULL) ||
                (!enum_set_typelib &&
                 item->type() == Item::FIELD_ITEM &&
                 (get_real_type(item) == MYSQL_TYPE_ENUM ||
                  get_real_type(item) == MYSQL_TYPE_SET) &&
                 ((Field_enum*)((Item_field *) item)->field)->typelib));
    if (!enum_set_typelib)
    {
      enum_set_typelib= ((Field_enum*)((Item_field *) item)->field)->typelib;
    }
  }
}


double Item_type_holder::val()
{
  DBUG_ASSERT(0); // should never be called
  return 0.0;
}


longlong Item_type_holder::val_int()
{
  DBUG_ASSERT(0); // should never be called
  return 0;
}


String *Item_type_holder::val_str(String*)
{
  DBUG_ASSERT(0); // should never be called
  return 0;
}

void Item_result_field::cleanup()
{
  DBUG_ENTER("Item_result_field::cleanup()");
  Item::cleanup();
  result_field= 0;
  DBUG_VOID_RETURN;
}

/*****************************************************************************
** Instantiate templates
*****************************************************************************/

#ifdef __GNUC__
template class List<Item>;
template class List_iterator<Item>;
template class List_iterator_fast<Item>;
template class List<List_item>;
#endif

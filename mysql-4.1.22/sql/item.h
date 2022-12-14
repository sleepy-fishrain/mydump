/* Copyright (C) 2000-2003 MySQL AB & MySQL Finland AB & TCX DataKonsult AB

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


#ifdef USE_PRAGMA_INTERFACE
#pragma interface			/* gcc class implementation */
#endif

class Protocol;
struct st_table_list;
void item_init(void);			/* Init item functions */


/*
   "Declared Type Collation"
   A combination of collation and its deriviation.
*/

enum Derivation
{
  DERIVATION_IGNORABLE= 5,
  DERIVATION_COERCIBLE= 4,
  DERIVATION_SYSCONST= 3,
  DERIVATION_IMPLICIT= 2,
  DERIVATION_NONE= 1,
  DERIVATION_EXPLICIT= 0
};

/*
  Flags for collation aggregation modes:
  MY_COLL_ALLOW_SUPERSET_CONV  - allow conversion to a superset
  MY_COLL_ALLOW_COERCIBLE_CONV - allow conversion of a coercible value
                                 (i.e. constant).
  MY_COLL_ALLOW_CONV           - allow any kind of conversion
                                 (combintion of the above two)
  MY_COLL_DISALLOW_NONE        - don't allow return DERIVATION_NONE
                                 (e.g. when aggregating for comparison)
  MY_COLL_CMP_CONV             - combination of MY_COLL_ALLOW_CONV
                                 and MY_COLL_DISALLOW_NONE
*/

#define MY_COLL_ALLOW_SUPERSET_CONV   1
#define MY_COLL_ALLOW_COERCIBLE_CONV  2
#define MY_COLL_ALLOW_CONV            3
#define MY_COLL_DISALLOW_NONE         4
#define MY_COLL_CMP_CONV              7

class DTCollation {
public:
  CHARSET_INFO     *collation;
  enum Derivation derivation;
  
  DTCollation()
  {
    collation= &my_charset_bin;
    derivation= DERIVATION_NONE;
  }
  DTCollation(CHARSET_INFO *collation_arg, Derivation derivation_arg)
  {
    collation= collation_arg;
    derivation= derivation_arg;
  }
  void set(DTCollation &dt)
  { 
    collation= dt.collation;
    derivation= dt.derivation;
  }
  void set(CHARSET_INFO *collation_arg, Derivation derivation_arg)
  {
    collation= collation_arg;
    derivation= derivation_arg;
  }
  void set(CHARSET_INFO *collation_arg)
  { collation= collation_arg; }
  void set(Derivation derivation_arg)
  { derivation= derivation_arg; }
  bool aggregate(DTCollation &dt, uint flags= 0);
  bool set(DTCollation &dt1, DTCollation &dt2, uint flags= 0)
  { set(dt1); return aggregate(dt2, flags); }
  const char *derivation_name() const
  {
    switch(derivation)
    {
      case DERIVATION_IGNORABLE: return "IGNORABLE";
      case DERIVATION_COERCIBLE: return "COERCIBLE";
      case DERIVATION_IMPLICIT:  return "IMPLICIT";
      case DERIVATION_SYSCONST:  return "SYSCONST";
      case DERIVATION_EXPLICIT:  return "EXPLICIT";
      case DERIVATION_NONE:      return "NONE";
      default: return "UNKNOWN";
    }
  }
};

typedef bool (Item::*Item_processor)(byte *arg);

class Item {
  Item(const Item &);			/* Prevent use of these */
  void operator=(Item &);
public:
  static void *operator new(size_t size) {return (void*) sql_alloc((uint) size); }
  static void *operator new(size_t size, MEM_ROOT *mem_root)
  { return (void*) alloc_root(mem_root, (uint) size); }
  static void operator delete(void *ptr,size_t size) {}
  static void operator delete(void *ptr, MEM_ROOT *mem_root) {}

  enum Type {FIELD_ITEM, FUNC_ITEM, SUM_FUNC_ITEM, STRING_ITEM,
	     INT_ITEM, REAL_ITEM, NULL_ITEM, VARBIN_ITEM,
	     COPY_STR_ITEM, FIELD_AVG_ITEM, DEFAULT_VALUE_ITEM,
	     PROC_ITEM,COND_ITEM, REF_ITEM, FIELD_STD_ITEM,
	     FIELD_VARIANCE_ITEM, INSERT_VALUE_ITEM,
             SUBSELECT_ITEM, ROW_ITEM, CACHE_ITEM, TYPE_HOLDER,
             PARAM_ITEM};

  enum cond_result { COND_UNDEF,COND_OK,COND_TRUE,COND_FALSE };
  
  /*
    str_values's main purpose is to be used to cache the value in
    save_in_field
  */
  String str_value;
  my_string name;			/* Name from select */
  Item *next;
  uint32 max_length;
  uint8 marker,decimals;
  my_bool maybe_null;			/* If item may be null */
  my_bool null_value;			/* if item is null */
  my_bool unsigned_flag;
  my_bool with_sum_func;
  my_bool fixed;                        /* If item fixed with fix_fields */
  DTCollation collation;

  // alloc & destruct is done as start of select using sql_alloc
  Item();
  /*
     Constructor used by Item_field, Item_ref & agregate (sum) functions.
     Used for duplicating lists in processing queries with temporary
     tables
     Also it used for Item_cond_and/Item_cond_or for creating
     top AND/OR ctructure of WHERE clause to protect it of
     optimisation changes in prepared statements
  */
  Item(THD *thd, Item *item);
  virtual ~Item() { name=0; }		/*lint -e1509 */
  void set_name(const char *str,uint length, CHARSET_INFO *cs);
  void init_make_field(Send_field *tmp_field,enum enum_field_types type);
  virtual void cleanup()
  {
    DBUG_ENTER("Item::cleanup");
    DBUG_PRINT("info", ("Type: %d", (int)type()));
    fixed=0;
    marker= 0;
    DBUG_VOID_RETURN;
  }
  virtual void make_field(Send_field *field);
  virtual bool fix_fields(THD *, struct st_table_list *, Item **);
  /*
    should be used in case where we are sure that we do not need
    complete fix_fields() procedure.
  */
  inline void quick_fix_field() { fixed= 1; }
  /* Function returns 1 on overflow and -1 on fatal errors */
  virtual int save_in_field(Field *field, bool no_conversions);
  virtual void save_org_in_field(Field *field)
  { (void) save_in_field(field, 1); }
  virtual int save_safe_in_field(Field *field)
  { return save_in_field(field, 1); }
  virtual bool send(Protocol *protocol, String *str);
  virtual bool eq(const Item *, bool binary_cmp) const;
  virtual Item_result result_type() const { return REAL_RESULT; }
  virtual Item_result cast_to_int_type() const { return result_type(); }
  virtual enum_field_types field_type() const;
  virtual enum Type type() const =0;
  /* valXXX methods must return NULL or 0 or 0.0 if null_value is set. */
  virtual double val()=0;
  virtual longlong val_int()=0;
  /*
    Return string representation of this item object.

    The argument to val_str() is an allocated buffer this or any
    nested Item object can use to store return value of this method.
    This buffer should only be used if the item itself doesn't have an
    own String buffer. In case when the item maintains it's own string
    buffer, it's preferrable to return it instead to minimize number of
    mallocs/memcpys.
    The caller of this method can modify returned string, but only in
    case when it was allocated on heap, (is_alloced() is true).  This
    allows the caller to efficiently use a buffer allocated by a child
    without having to allocate a buffer of it's own. The buffer, given
    to val_str() as agrument, belongs to the caller and is later used
    by the caller at it's own choosing.
    A few implications from the above:
    - unless you return a string object which only points to your buffer
      but doesn't manages it you should be ready that it will be
      modified.
    - even for not allocated strings (is_alloced() == false) the caller
      can change charset (see Item_func_{typecast/binary}. XXX: is this
      a bug?
    - still you should try to minimize data copying and return internal
      object whenever possible.
  */
  virtual String *val_str(String*)=0;
  virtual Field *get_tmp_table_field() { return 0; }
  virtual Field *tmp_table_field(TABLE *t_arg) { return 0; }
  virtual const char *full_name() const { return name ? name : "???"; }
  virtual double  val_result() { return val(); }
  virtual longlong val_int_result() { return val_int(); }
  virtual String *str_result(String* tmp) { return val_str(tmp); }
  /* bit map of tables used by item */
  virtual table_map used_tables() const { return (table_map) 0L; }
  /*
    Return table map of tables that can't be NULL tables (tables that are
    used in a context where if they would contain a NULL row generated
    by a LEFT or RIGHT join, the item would not be true).
    This expression is used on WHERE item to determinate if a LEFT JOIN can be
    converted to a normal join.
    Generally this function should return used_tables() if the function
    would return null if any of the arguments are null
    As this is only used in the beginning of optimization, the value don't
    have to be updated in update_used_tables()
  */
  virtual table_map not_null_tables() const { return used_tables(); }
  /*
    Returns true if this is a simple constant item like an integer, not
    a constant expression. Used in the optimizer to propagate basic constants.
  */
  virtual bool basic_const_item() const { return 0; }
  /* cloning of constant items (0 if it is not const) */
  virtual Item *new_item() { return 0; }
  virtual cond_result eq_cmp_result() const { return COND_OK; }
  inline uint float_length(uint decimals_par) const
  { return decimals != NOT_FIXED_DEC ? (DBL_DIG+2+decimals_par) : DBL_DIG+8;}
  /* 
    Returns true if this is constant (during query execution, i.e. its value
    will not change until next fix_fields) and its value is known.
  */
  virtual bool const_item() const { return used_tables() == 0; }
  /* 
    Returns true if this is constant but its value may be not known yet.
    (Can be used for parameters of prep. stmts or of stored procedures.)
  */
  virtual bool const_during_execution() const 
  { return (used_tables() & ~PARAM_TABLE_BIT) == 0; }
  virtual void print(String *str_arg) { str_arg->append(full_name()); }
  void print_item_w_name(String *);
  virtual void update_used_tables() {}
  virtual void split_sum_func(THD *thd, Item **ref_pointer_array,
                              List<Item> &fields) {}
  /* Called for items that really have to be split */
  void split_sum_func2(THD *thd, Item **ref_pointer_array, List<Item> &fields,
                       Item **ref);
  virtual bool get_date(TIME *ltime,uint fuzzydate);
  virtual bool get_time(TIME *ltime);
  virtual bool get_date_result(TIME *ltime,uint fuzzydate)
  { return get_date(ltime,fuzzydate); }
  /*
    This function is used only in Item_func_isnull/Item_func_isnotnull
    (implementations of IS NULL/IS NOT NULL clauses). Item_func_is{not}null
    calls this method instead of one of val/result*() methods, which
    normally will set null_value. This allows to determine nullness of
    a complex expression without fully evaluating it.
    Any new item which can be NULL must implement this call.
  */
  virtual bool is_null() { return 0; }

  /*
    Inform the item that there will be no distinction between its result
    being FALSE or NULL.

    NOTE
      This function will be called for eg. Items that are top-level AND-parts
      of the WHERE clause. Items implementing this function (currently
      Item_cond_and and subquery-related item) enable special optimizations
      when they are "top level".
  */
  virtual void top_level_item() {}
  /*
    set field of temporary table for Item which can be switched on temporary
    table during query processing (groupping and so on)
  */
  virtual void set_result_field(Field *field) {}
  virtual bool is_result_field() { return 0; }
  virtual bool is_bool_func() { return 0; }
  virtual void save_in_result_field(bool no_conversions) {}
  /*
    set value of aggegate function in case of no rows for groupping were found
  */
  virtual void no_rows_in_result() {}
  virtual Item *copy_or_same(THD *thd) { return this; }
  virtual Item *copy_andor_structure(THD *thd) { return this; }
  virtual Item *real_item() { return this; }
  virtual Item *get_tmp_table_item(THD *thd) { return copy_or_same(thd); }

  static CHARSET_INFO *default_charset();
  virtual CHARSET_INFO *compare_collation() { return NULL; }

  virtual bool walk(Item_processor processor, byte *arg)
  {
    return (this->*processor)(arg);
  }

  virtual bool remove_dependence_processor(byte * arg) { return 0; }
  virtual bool remove_fixed(byte * arg) { fixed= 0; return 0; }
  
  // Row emulation
  virtual uint cols() { return 1; }
  virtual Item* el(uint i) { return this; }
  virtual Item** addr(uint i) { return 0; }
  virtual bool check_cols(uint c);
  // It is not row => null inside is impossible
  virtual bool null_inside() { return 0; }
  // used in row subselects to get value of elements
  virtual void bring_value() {}

  Field *tmp_table_field_from_field_type(TABLE *table);

  virtual Item *neg_transformer(THD *thd) { return NULL; }
  virtual Item *safe_charset_converter(CHARSET_INFO *tocs);
  void delete_self()
  {
    cleanup();
    delete this;
  }
  /*
    result_as_longlong() must return TRUE for Items representing DATE/TIME
    functions and DATE/TIME table fields.
    Those Items have result_type()==STRING_RESULT (and not INT_RESULT), but
    their values should be compared as integers (because the integer
    representation is more precise than the string one).
  */
  virtual bool result_as_longlong() { return FALSE; }
};


bool agg_item_collations(DTCollation &c, const char *name,
                         Item **items, uint nitems, uint flags= 0);
bool agg_item_collations_for_comparison(DTCollation &c, const char *name,
                                        Item **items, uint nitems,
                                        uint flags= 0);
bool agg_item_charsets(DTCollation &c, const char *name,
                       Item **items, uint nitems, uint flags= 0);


class Item_num: public Item
{
public:
  virtual Item_num *neg()= 0;
  Item *safe_charset_converter(CHARSET_INFO *tocs);
};

#define NO_CACHED_FIELD_INDEX ((uint)(-1))

class st_select_lex;
class Item_ident :public Item
{
protected:
  /* 
    We have to store initial values of db_name, table_name and field_name
    to be able to restore them during cleanup() because they can be 
    updated during fix_fields() to values from Field object and life-time 
    of those is shorter than life-time of Item_field.
  */
  const char *orig_db_name;
  const char *orig_table_name;
  const char *orig_field_name;
public:
  const char *db_name;
  const char *table_name;
  const char *field_name;
  /* 
    Cached value of index for this field in table->field array, used by prep. 
    stmts for speeding up their re-execution. Holds NO_CACHED_FIELD_INDEX 
    if index value is not known.
  */
  uint cached_field_index;
  /*
    Cached pointer to table which contains this field, used for the same reason
    by prep. stmt. too in case then we have not-fully qualified field.
    0 - means no cached value.
  */
  TABLE_LIST *cached_table;
  st_select_lex *depended_from;
  Item_ident(const char *db_name_par,const char *table_name_par,
	     const char *field_name_par);
  Item_ident(THD *thd, Item_ident *item);
  const char *full_name() const;
  void cleanup();
  bool remove_dependence_processor(byte * arg);
};


class Item_field :public Item_ident
{
  void set_field(Field *field);
public:
  Field *field,*result_field;

  Item_field(const char *db_par,const char *table_name_par,
	     const char *field_name_par)
    :Item_ident(db_par,table_name_par,field_name_par),
     field(0), result_field(0)
  { collation.set(DERIVATION_IMPLICIT); }
  /*
    Constructor needed to process subselect with temporary tables (see Item)
  */
  Item_field(THD *thd, Item_field *item);
  /*
    Constructor used inside setup_wild(), ensures that field, table,
    and database names will live as long as Item_field (this is important
    in prepared statements).
  */
  Item_field(THD *thd, Field *field);
  /*
    If this constructor is used, fix_fields() won't work, because
    db_name, table_name and column_name are unknown. It's necessary to call
    reset_field() before fix_fields() for all fields created this way.
  */
  Item_field(Field *field);
  enum Type type() const { return FIELD_ITEM; }
  bool eq(const Item *item, bool binary_cmp) const;
  double val();
  longlong val_int();
  String *val_str(String*);
  double val_result();
  longlong val_int_result();
  String *str_result(String* tmp);
  bool send(Protocol *protocol, String *str_arg);
  void reset_field(Field *f);
  bool fix_fields(THD *, struct st_table_list *, Item **);
  void make_field(Send_field *tmp_field);
  int save_in_field(Field *field,bool no_conversions);
  void save_org_in_field(Field *field);
  table_map used_tables() const;
  enum Item_result result_type () const
  {
    return field->result_type();
  }
  Item_result cast_to_int_type() const
  {
    return field->cast_to_int_type();
  }
  enum_field_types field_type() const
  {
    return field->type();
  }
  Field *get_tmp_table_field() { return result_field; }
  Field *tmp_table_field(TABLE *t_arg) { return result_field; }
  bool get_date(TIME *ltime,uint fuzzydate);
  bool get_date_result(TIME *ltime,uint fuzzydate);
  bool get_time(TIME *ltime);
  bool is_null() { return field->is_null(); }
  Item *get_tmp_table_item(THD *thd);
  void cleanup();
  inline uint32 max_disp_length() { return field->max_length(); }
  bool result_as_longlong()
  {
    return field->can_be_compared_as_longlong();
  }
  friend class Item_default_value;
  friend class Item_insert_value;
  friend class st_select_lex_unit;
};

class Item_null :public Item
{
public:
  Item_null(char *name_par=0)
  {
    maybe_null= null_value= TRUE;
    max_length= 0;
    name= name_par ? name_par : (char*) "NULL";
    fixed= 1;
    collation.set(&my_charset_bin, DERIVATION_IGNORABLE);
  }
  enum Type type() const { return NULL_ITEM; }
  bool eq(const Item *item, bool binary_cmp) const;
  double val();
  longlong val_int();
  String *val_str(String *str);
  int save_in_field(Field *field, bool no_conversions);
  int save_safe_in_field(Field *field);
  bool send(Protocol *protocol, String *str);
  enum Item_result result_type () const { return STRING_RESULT; }
  enum_field_types field_type() const   { return MYSQL_TYPE_NULL; }
  // to prevent drop fixed flag (no need parent cleanup call)
  void cleanup() {}
  bool basic_const_item() const { return 1; }
  Item *new_item() { return new Item_null(name); }
  bool is_null() { return 1; }
  void print(String *str) { str->append("NULL", 4); }
  Item *safe_charset_converter(CHARSET_INFO *tocs);
};

class Item_null_result :public Item_null
{
public:
  Field *result_field;
  Item_null_result() : Item_null(), result_field(0) {}
  bool is_result_field() { return result_field != 0; }
  void save_in_result_field(bool no_conversions)
  {
    save_in_field(result_field, no_conversions);
  }
};  

/* Item represents one placeholder ('?') of prepared statement */

class Item_param :public Item
{
public:
  enum enum_item_param_state
  {
    NO_VALUE, NULL_VALUE, INT_VALUE, REAL_VALUE,
    STRING_VALUE, TIME_VALUE, LONG_DATA_VALUE
  } state;

  /*
    A buffer for string and long data values. Historically all allocated
    values returned from val_str() were treated as eligible to
    modification. I. e. in some cases Item_func_concat can append it's
    second argument to return value of the first one. Because of that we
    can't return the original buffer holding string data from val_str(),
    and have to have one buffer for data and another just pointing to
    the data. This is the latter one and it's returned from val_str().
    Can not be declared inside the union as it's not a POD type.
  */
  String str_value_ptr;
  union
  {
    longlong integer;
    double   real;
    /*
      Character sets conversion info for string values.
      Character sets of client and connection defined at bind time are used
      for all conversions, even if one of them is later changed (i.e.
      between subsequent calls to mysql_stmt_execute).
    */
    struct CONVERSION_INFO
    {
      CHARSET_INFO *character_set_client;
      CHARSET_INFO *character_set_of_placeholder;
      /*
        This points at character set of connection if conversion
        to it is required (i. e. if placeholder typecode is not BLOB).
        Otherwise it's equal to character_set_client (to simplify
        check in convert_str_value()).
      */
      CHARSET_INFO *final_character_set_of_str_value;
    } cs_info;
    TIME     time;
  } value;

  /* Cached values for virtual methods to save us one switch.  */
  enum Item_result item_result_type;
  enum Type item_type;

  /*
    Used when this item is used in a temporary table.
    This is NOT placeholder metadata sent to client, as this value
    is assigned after sending metadata (in setup_one_conversion_function).
    For example in case of 'SELECT ?' you'll get MYSQL_TYPE_STRING both
    in result set and placeholders metadata, no matter what type you will
    supply for this placeholder in mysql_stmt_execute.
  */
  enum enum_field_types param_type;
  /*
    Offset of placeholder inside statement text. Used to create
    no-placeholders version of this statement for the binary log.
  */
  uint pos_in_query;

  Item_param(uint pos_in_query_arg);

  enum Item_result result_type () const { return item_result_type; }
  enum Type type() const { return item_type; }
  enum_field_types field_type() const { return param_type; }

  double val();
  longlong val_int();
  String *val_str(String*);
  bool get_time(TIME *tm);
  bool get_date(TIME *tm, uint fuzzydate);
  int  save_in_field(Field *field, bool no_conversions);

  void set_null();
  void set_int(longlong i, uint32 max_length_arg);
  void set_double(double i);
  bool set_str(const char *str, ulong length);
  bool set_longdata(const char *str, ulong length);
  void set_time(TIME *tm, timestamp_type type, uint32 max_length_arg);
  bool set_from_user_var(THD *thd, const user_var_entry *entry);
  void reset();
  /*
    Assign placeholder value from bind data.
    Note, that 'len' has different semantics in embedded library (as we
    don't need to check that packet is not broken there). See
    sql_prepare.cc for details.
  */
  void (*set_param_func)(Item_param *param, uchar **pos, ulong len);

  const String *query_val_str(String *str) const;

  bool convert_str_value(THD *thd);

  /*
    If value for parameter was not set we treat it as non-const
    so noone will use parameters value in fix_fields still
    parameter is constant during execution.
  */
  virtual table_map used_tables() const
  { return state != NO_VALUE ? (table_map)0 : PARAM_TABLE_BIT; }
  void print(String *str) { str->append('?'); }
  bool is_null()
  { DBUG_ASSERT(state != NO_VALUE); return state == NULL_VALUE; }
  bool basic_const_item() const;
  /*
    This method is used to make a copy of a basic constant item when
    propagating constants in the optimizer. The reason to create a new
    item and not use the existing one is not precisely known (2005/04/16).
    Probably we are trying to preserve tree structure of items, in other
    words, avoid pointing at one item from two different nodes of the tree.
    Return a new basic constant item if parameter value is a basic
    constant, assert otherwise. This method is called only if
    basic_const_item returned TRUE.
  */
  Item *new_item();
  Item *safe_charset_converter(CHARSET_INFO *tocs);
  /*
    Implement by-value equality evaluation if parameter value
    is set and is a basic constant (integer, real or string).
    Otherwise return FALSE.
  */
  bool eq(const Item *item, bool binary_cmp) const;
};


class Item_int :public Item_num
{
public:
  longlong value;
  Item_int(int32 i,uint length=11) :value((longlong) i)
    { max_length=length; fixed= 1; }
#ifdef HAVE_LONG_LONG
  Item_int(longlong i,uint length=21) :value(i)
    { max_length=length; fixed= 1;}
#endif
  Item_int(const char *str_arg,longlong i,uint length) :value(i)
    { max_length=length; name=(char*) str_arg; fixed= 1; }
  Item_int(const char *str_arg, uint length=64);
  enum Type type() const { return INT_ITEM; }
  enum Item_result result_type () const { return INT_RESULT; }
  enum_field_types field_type() const { return MYSQL_TYPE_LONGLONG; }
  longlong val_int() { DBUG_ASSERT(fixed == 1); return value; }
  double val() { DBUG_ASSERT(fixed == 1); return (double) value; }
  String *val_str(String*);
  int save_in_field(Field *field, bool no_conversions);
  bool basic_const_item() const { return 1; }
  Item *new_item() { return new Item_int(name,value,max_length); }
  // to prevent drop fixed flag (no need parent cleanup call)
  void cleanup() {}
  void print(String *str);
  Item_num *neg() { value= -value; return this; }
  bool eq(const Item *, bool binary_cmp) const;
};


class Item_uint :public Item_int
{
public:
  Item_uint(const char *str_arg, uint length);
  Item_uint(const char *str_arg, longlong i, uint length);
  Item_uint(uint32 i) :Item_int((longlong) i, 10) 
    { unsigned_flag= 1; }
  double val()
    { DBUG_ASSERT(fixed == 1); return ulonglong2double((ulonglong)value); }
  String *val_str(String*);
  Item *new_item() { return new Item_uint(name,max_length); }
  int save_in_field(Field *field, bool no_conversions);
  void print(String *str);
  Item_num *neg ();
};


class Item_real :public Item_num
{
public:
  double value;
  // Item_real() :value(0) {}
  Item_real(const char *str_arg, uint length) :value(my_atof(str_arg))
  {
    name=(char*) str_arg;
    decimals=(uint8) nr_of_decimals(str_arg);
    max_length=length;
    fixed= 1;
  }
  Item_real(const char *str,double val_arg,uint decimal_par,uint length)
    :value(val_arg)
  {
    name=(char*) str;
    decimals=(uint8) decimal_par;
    max_length=length;
    fixed= 1;
  }
  Item_real(double value_par) :value(value_par) { fixed= 1; }
  int save_in_field(Field *field, bool no_conversions);
  enum Type type() const { return REAL_ITEM; }
  enum_field_types field_type() const { return MYSQL_TYPE_DOUBLE; }
  double val() { DBUG_ASSERT(fixed == 1); return value; }
  longlong val_int()
  {
    DBUG_ASSERT(fixed == 1);
    if (value <= (double) LONGLONG_MIN)
    {
       return LONGLONG_MIN;
    }
    else if (value >= (double) (ulonglong) LONGLONG_MAX)
    {
      return LONGLONG_MAX;
    }
    return (longlong) (value+(value > 0 ? 0.5 : -0.5));
  }
  String *val_str(String*);
  bool basic_const_item() const { return 1; }
  // to prevent drop fixed flag (no need parent cleanup call)
  void cleanup() {}
  Item *new_item() { return new Item_real(name,value,decimals,max_length); }
  Item_num *neg() { value= -value; return this; }
  bool eq(const Item *, bool binary_cmp) const;
};


class Item_float :public Item_real
{
public:
  Item_float(const char *str,uint length) :Item_real(str,length)
  {
    decimals=NOT_FIXED_DEC;
    max_length=DBL_DIG+8;
  }
};

class Item_string :public Item
{
public:
  Item_string(const char *str,uint length,
  	      CHARSET_INFO *cs, Derivation dv= DERIVATION_COERCIBLE)
  {
    collation.set(cs, dv);
    str_value.set_or_copy_aligned(str,length,cs);
    /*
      We have to have a different max_length than 'length' here to
      ensure that we get the right length if we do use the item
      to create a new table. In this case max_length must be the maximum
      number of chars for a string of this type because we in create_field::
      divide the max_length with mbmaxlen).
    */
    max_length= str_value.numchars()*cs->mbmaxlen;
    set_name(str, length, cs);
    decimals=NOT_FIXED_DEC;
    // it is constant => can be used without fix_fields (and frequently used)
    fixed= 1;
  }
  Item_string(const char *name_par, const char *str, uint length,
	      CHARSET_INFO *cs, Derivation dv= DERIVATION_COERCIBLE)
  {
    collation.set(cs, dv);
    str_value.set_or_copy_aligned(str,length,cs);
    max_length= str_value.numchars()*cs->mbmaxlen;
    set_name(name_par,0,cs);
    decimals=NOT_FIXED_DEC;
    // it is constant => can be used without fix_fields (and frequently used)
    fixed= 1;
  }
  enum Type type() const { return STRING_ITEM; }
  double val()
  {
    DBUG_ASSERT(fixed == 1);
    int err;
    char *end_not_used;
    return my_strntod(str_value.charset(), (char*) str_value.ptr(),
		      str_value.length(), &end_not_used, &err);
  }
  longlong val_int()
  {
    DBUG_ASSERT(fixed == 1);
    int err;
    return my_strntoll(str_value.charset(), str_value.ptr(),
		       str_value.length(), 10, (char**) 0, &err);
  }
  String *val_str(String*)
  {
    DBUG_ASSERT(fixed == 1);
    return (String*) &str_value;
  }
  int save_in_field(Field *field, bool no_conversions);
  enum Item_result result_type () const { return STRING_RESULT; }
  enum_field_types field_type() const { return MYSQL_TYPE_STRING; }
  bool basic_const_item() const { return 1; }
  bool eq(const Item *item, bool binary_cmp) const;
  Item *new_item() 
  {
    return new Item_string(name, str_value.ptr(), 
    			   str_value.length(), collation.collation);
  }
  Item *safe_charset_converter(CHARSET_INFO *tocs);
  String *const_string() { return &str_value; }
  inline void append(char *str, uint length) { str_value.append(str, length); }
  void print(String *str);
  // to prevent drop fixed flag (no need parent cleanup call)
  void cleanup() {}
};

/* for show tables */

class Item_datetime :public Item_string
{
public:
  Item_datetime(const char *item_name): Item_string(item_name,"",0,
  						    &my_charset_bin)
  { max_length=19;}
  enum_field_types field_type() const { return MYSQL_TYPE_DATETIME; }
};

class Item_empty_string :public Item_string
{
public:
  Item_empty_string(const char *header,uint length, CHARSET_INFO *cs= NULL) :
    Item_string("",0, cs ? cs : &my_charset_bin)
    { name=(char*) header; max_length= cs ? length * cs->mbmaxlen : length; }
  void make_field(Send_field *field);
};

class Item_return_int :public Item_int
{
  enum_field_types int_field_type;
public:
  Item_return_int(const char *name, uint length,
		  enum_field_types field_type_arg)
    :Item_int(name, 0, length), int_field_type(field_type_arg)
  {
    unsigned_flag=1;
  }
  enum_field_types field_type() const { return int_field_type; }
};


class Item_varbinary :public Item
{
public:
  Item_varbinary(const char *str,uint str_length);
  enum Type type() const { return VARBIN_ITEM; }
  double val()
    { DBUG_ASSERT(fixed == 1); return (double) Item_varbinary::val_int(); }
  longlong val_int();
  bool basic_const_item() const { return 1; }
  String *val_str(String*) { DBUG_ASSERT(fixed == 1); return &str_value; }
  int save_in_field(Field *field, bool no_conversions);
  enum Item_result result_type () const { return STRING_RESULT; }
  enum Item_result cast_to_int_type() const { return INT_RESULT; }
  enum_field_types field_type() const { return MYSQL_TYPE_STRING; }
  // to prevent drop fixed flag (no need parent cleanup call)
  void cleanup() {}
  bool eq(const Item *item, bool binary_cmp) const;
  virtual Item *safe_charset_converter(CHARSET_INFO *tocs);
};


class Item_result_field :public Item	/* Item with result field */
{
public:
  Field *result_field;				/* Save result here */
  Item_result_field() :result_field(0) {}
  // Constructor used for Item_sum/Item_cond_and/or (see Item comment)
  Item_result_field(THD *thd, Item_result_field *item):
    Item(thd, item), result_field(item->result_field)
  {}
  ~Item_result_field() {}			/* Required with gcc 2.95 */
  Field *get_tmp_table_field() { return result_field; }
  Field *tmp_table_field(TABLE *t_arg) { return result_field; }
  table_map used_tables() const { return 1; }
  virtual void fix_length_and_dec()=0;
  void set_result_field(Field *field) { result_field= field; }
  bool is_result_field() { return 1; }
  void save_in_result_field(bool no_conversions)
  {
    save_in_field(result_field, no_conversions);
  }
  void cleanup();
};


class Item_ref :public Item_ident
{
protected:
  void set_properties();
public:
  Field *result_field;			 /* Save result here */
  Item **ref;
  Item_ref(const char *db_par, const char *table_name_par,
           const char *field_name_par)
    :Item_ident(db_par, table_name_par, field_name_par), ref(0) {}
  /*
    This constructor is used in two scenarios:
    A) *item = NULL
      No initialization is performed, fix_fields() call will be necessary.
      
    B) *item points to an Item this Item_ref will refer to. This is 
      used for GROUP BY. fix_fields() will not be called in this case,
      so we call set_properties to make this item "fixed". set_properties
      performs a subset of action Item_ref::fix_fields does, and this subset
      is enough for Item_ref's used in GROUP BY.
    
    TODO we probably fix a superset of problems like in BUG#6658. Check this 
         with Bar, and if we have a more broader set of problems like this.
  */
  Item_ref(Item **item, const char *table_name_par, const char *field_name_par)
    :Item_ident(NullS, table_name_par, field_name_par), ref(item)
  {
    DBUG_ASSERT(item);
    if (*item)
      set_properties();
  }

  /* Constructor need to process subselect with temporary tables (see Item) */
  Item_ref(THD *thd, Item_ref *item) :Item_ident(thd, item), ref(item->ref) {}
  enum Type type() const		{ return REF_ITEM; }
  bool eq(const Item *item, bool binary_cmp) const
  { return ref && (*ref)->eq(item, binary_cmp); }
  double val()
  {
    DBUG_ASSERT(fixed);
    double tmp=(*ref)->val_result();
    null_value=(*ref)->null_value;
    return tmp;
  }
  longlong val_int()
  {
    DBUG_ASSERT(fixed);
    longlong tmp=(*ref)->val_int_result();
    null_value=(*ref)->null_value;
    return tmp;
  }
  String *val_str(String* tmp)
  {
    DBUG_ASSERT(fixed);
    tmp=(*ref)->str_result(tmp);
    null_value=(*ref)->null_value;
    return tmp;
  }
  bool is_null()
  {
    DBUG_ASSERT(fixed);
    (void) (*ref)->val_int_result();
    return (*ref)->null_value;
  }
  bool get_date(TIME *ltime,uint fuzzydate)
  {
    DBUG_ASSERT(fixed);
    return (null_value=(*ref)->get_date_result(ltime,fuzzydate));
  }
  bool send(Protocol *prot, String *tmp){ return (*ref)->send(prot, tmp); }
  void make_field(Send_field *field)	{ (*ref)->make_field(field); }
  bool fix_fields(THD *, struct st_table_list *, Item **);
  int save_in_field(Field *field, bool no_conversions)
  { return (*ref)->save_in_field(field, no_conversions); }
  void save_org_in_field(Field *field)	{ (*ref)->save_org_in_field(field); }
  enum Item_result result_type () const { return (*ref)->result_type(); }
  enum_field_types field_type() const   { return (*ref)->field_type(); }
  Field *get_tmp_table_field() { return result_field; }
  table_map used_tables() const		
  { 
    return depended_from ? OUTER_REF_TABLE_BIT : (*ref)->used_tables(); 
  }
  void set_result_field(Field *field)	{ result_field= field; }
  bool is_result_field() { return 1; }
  void save_in_result_field(bool no_conversions)
  {
    (*ref)->save_in_field(result_field, no_conversions);
  }
  Item *real_item() { return *ref; }
  void print(String *str);
  bool result_as_longlong()
  {
    return (*ref)->result_as_longlong();
  }
};


/*
  The same as Item_ref, but get value from val_* family of method to get
  value of item on which it referred instead of result* family.
*/
class Item_direct_ref :public Item_ref
{
public:
  Item_direct_ref(Item **item, const char *table_name_par,
                  const char *field_name_par)
    :Item_ref(item, table_name_par, field_name_par) {}
  /* Constructor need to process subselect with temporary tables (see Item) */
  Item_direct_ref(THD *thd, Item_direct_ref *item) : Item_ref(thd, item) {}

  double val()
  {
    double tmp=(*ref)->val();
    null_value=(*ref)->null_value;
    return tmp;
  }
  longlong val_int()
  {
    longlong tmp=(*ref)->val_int();
    null_value=(*ref)->null_value;
    return tmp;
  }
  String *val_str(String* tmp)
  {
    tmp=(*ref)->val_str(tmp);
    null_value=(*ref)->null_value;
    return tmp;
  }
  bool is_null()
  {
    (void) (*ref)->val_int();
    return (*ref)->null_value;
  }
  bool get_date(TIME *ltime,uint fuzzydate)
  {
    return (null_value=(*ref)->get_date(ltime,fuzzydate));
  }
};


class Item_in_subselect;

class Item_ref_null_helper: public Item_ref
{
protected:
  Item_in_subselect* owner;
public:
  Item_ref_null_helper(Item_in_subselect* master, Item **item,
		       const char *table_name_par, const char *field_name_par):
    Item_ref(item, table_name_par, field_name_par), owner(master) {}
  double val();
  longlong val_int();
  String* val_str(String* s);
  bool get_date(TIME *ltime, uint fuzzydate);
  void print(String *str);
  /*
    we add RAND_TABLE_BIT to prevent moving this item from HAVING to WHERE
  */
  table_map used_tables() const
  {
    return (depended_from ?
            OUTER_REF_TABLE_BIT :
            (*ref)->used_tables() | RAND_TABLE_BIT);
  }
};

/*
  The following class is used to optimize comparing of date and bigint columns
  We need to save the original item ('ref') to be able to call
  ref->save_in_field(). This is used to create index search keys.
  
  An instance of Item_int_with_ref may have signed or unsigned integer value.
  
*/

class Item_int_with_ref :public Item_int
{
  Item *ref;
public:
  Item_int_with_ref(longlong i, Item *ref_arg) :Item_int(i), ref(ref_arg)
  {
    unsigned_flag= ref_arg->unsigned_flag;
  }
  int save_in_field(Field *field, bool no_conversions)
  {
    return ref->save_in_field(field, no_conversions);
  }
  Item *new_item();
};


#include "gstream.h"
#include "spatial.h"
#include "item_sum.h"
#include "item_func.h"
#include "item_row.h"
#include "item_cmpfunc.h"
#include "item_strfunc.h"
#include "item_geofunc.h"
#include "item_timefunc.h"
#include "item_uniq.h"
#include "item_subselect.h"

class Item_copy_string :public Item
{
  enum enum_field_types cached_field_type;
public:
  Item *item;
  Item_copy_string(Item *i) :item(i)
  {
    null_value=maybe_null=item->maybe_null;
    decimals=item->decimals;
    max_length=item->max_length;
    name=item->name;
    cached_field_type= item->field_type();
  }
  enum Type type() const { return COPY_STR_ITEM; }
  enum Item_result result_type () const { return STRING_RESULT; }
  enum_field_types field_type() const { return cached_field_type; }
  double val()
  {
    int err;
    char *end_not_used;
    return (null_value ? 0.0 :
	    my_strntod(str_value.charset(), (char*) str_value.ptr(),
		       str_value.length(), &end_not_used, &err));
  }
  longlong val_int()
  { 
    int err;
    return null_value ? LL(0) : my_strntoll(str_value.charset(),str_value.ptr(),str_value.length(),10, (char**) 0,&err); 
  }
  String *val_str(String*);
  void make_field(Send_field *field) { item->make_field(field); }
  void copy();
  int save_in_field(Field *field, bool no_conversions);
  table_map used_tables() const { return (table_map) 1L; }
  bool const_item() const { return 0; }
  bool is_null() { return null_value; }
};


class Item_buff :public Sql_alloc
{
public:
  my_bool null_value;
  Item_buff() :null_value(0) {}
  virtual bool cmp(void)=0;
  virtual ~Item_buff(); /*line -e1509 */
};

class Item_str_buff :public Item_buff
{
  Item *item;
  String value,tmp_value;
public:
  Item_str_buff(THD *thd, Item *arg);
  bool cmp(void);
  ~Item_str_buff();				// Deallocate String:s
};


class Item_real_buff :public Item_buff
{
  Item *item;
  double value;
public:
  Item_real_buff(Item *item_par) :item(item_par),value(0.0) {}
  bool cmp(void);
};

class Item_int_buff :public Item_buff
{
  Item *item;
  longlong value;
public:
  Item_int_buff(Item *item_par) :item(item_par),value(0) {}
  bool cmp(void);
};


class Item_field_buff :public Item_buff
{
  char *buff;
  Field *field;
  uint length;

public:
  Item_field_buff(Item_field *item)
  {
    field=item->field;
    buff= (char*) sql_calloc(length=field->pack_length());
  }
  bool cmp(void);
};

class Item_default_value : public Item_field
{
public:
  Item *arg;
  Item_default_value() :
    Item_field((const char *)NULL, (const char *)NULL, (const char *)NULL), arg(NULL) {}
  Item_default_value(Item *a) :
    Item_field((const char *)NULL, (const char *)NULL, (const char *)NULL), arg(a) {}
  enum Type type() const { return DEFAULT_VALUE_ITEM; }
  bool eq(const Item *item, bool binary_cmp) const;
  bool fix_fields(THD *, struct st_table_list *, Item **);
  void print(String *str);
  int save_in_field(Field *field_arg, bool no_conversions)
  {
    if (!arg)
    {
      field_arg->set_default();
      return 0;
    }
    return Item_field::save_in_field(field_arg, no_conversions);
  }
  table_map used_tables() const { return (table_map)0L; }
  
  bool walk(Item_processor processor, byte *args)
  {
    return arg->walk(processor, args) ||
      (this->*processor)(args);
  }
};

class Item_insert_value : public Item_field
{
public:
  Item *arg;
  Item_insert_value(Item *a) :
    Item_field((const char *)NULL, (const char *)NULL, (const char *)NULL), arg(a) {}
  bool eq(const Item *item, bool binary_cmp) const;
  bool fix_fields(THD *, struct st_table_list *, Item **);
  void print(String *str);
  int save_in_field(Field *field_arg, bool no_conversions)
  {
    return Item_field::save_in_field(field_arg, no_conversions);
  }
  /* 
   We use RAND_TABLE_BIT to prevent Item_insert_value from
   being treated as a constant and precalculated before execution
  */
  table_map used_tables() const { return RAND_TABLE_BIT; }

  bool walk(Item_processor processor, byte *args)
  {
    return arg->walk(processor, args) ||
	    (this->*processor)(args);
  }
};

class Item_cache: public Item
{
protected:
  Item *example;
  table_map used_table_map;
public:
  Item_cache(): example(0), used_table_map(0) {fixed= 1; null_value= 1;}

  void set_used_tables(table_map map) { used_table_map= map; }

  virtual bool allocate(uint i) { return 0; }
  virtual bool setup(Item *item)
  {
    example= item;
    max_length= item->max_length;
    decimals= item->decimals;
    collation.set(item->collation);
    return 0;
  };
  virtual void store(Item *)= 0;
  enum Type type() const { return CACHE_ITEM; }
  static Item_cache* get_cache(Item_result type);
  table_map used_tables() const { return used_table_map; }
  virtual void keep_array() {}
  // to prevent drop fixed flag (no need parent cleanup call)
  void cleanup() {}
  void print(String *str);
};

class Item_cache_int: public Item_cache
{
  longlong value;
public:
  Item_cache_int(): Item_cache(), value(0) {}
  
  void store(Item *item);
  double val() { DBUG_ASSERT(fixed == 1); return (double) value; }
  longlong val_int() { DBUG_ASSERT(fixed == 1); return value; }
  String* val_str(String *str)
  {
    DBUG_ASSERT(fixed == 1);
    str->set(value, default_charset());
    return str;
  }
  enum Item_result result_type() const { return INT_RESULT; }
};

class Item_cache_real: public Item_cache
{
  double value;
public:
  Item_cache_real(): Item_cache(), value(0) {}

  void store(Item *item);
  double val() { DBUG_ASSERT(fixed == 1); return value; }
  longlong val_int()
  {
    DBUG_ASSERT(fixed == 1);
    return (longlong) (value+(value > 0 ? 0.5 : -0.5));
  }
  String* val_str(String *str)
  {
    str->set(value, decimals, default_charset());
    return str;
  }
  enum Item_result result_type() const { return REAL_RESULT; }
};

class Item_cache_str: public Item_cache
{
  char buffer[80];
  String *value, value_buff;
public:
  Item_cache_str(): Item_cache(), value(0) { }
  
  void store(Item *item);
  double val();
  longlong val_int();
  String* val_str(String *) { DBUG_ASSERT(fixed == 1); return value; }
  enum Item_result result_type() const { return STRING_RESULT; }
  CHARSET_INFO *charset() const { return value->charset(); };
};

class Item_cache_row: public Item_cache
{
  Item_cache  **values;
  uint item_count;
  bool save_array;
public:
  Item_cache_row()
    :Item_cache(), values(0), item_count(2), save_array(0) {}
  
  /*
    'allocate' used only in row transformer, to preallocate space for row 
    cache.
  */
  bool allocate(uint num);
  /*
    'setup' is needed only by row => it not called by simple row subselect
    (only by IN subselect (in subselect optimizer))
  */
  bool setup(Item *item);
  void store(Item *item);
  void illegal_method_call(const char *);
  void make_field(Send_field *)
  {
    illegal_method_call((const char*)"make_field");
  };
  double val()
  {
    illegal_method_call((const char*)"val");
    return 0;
  };
  longlong val_int()
  {
    illegal_method_call((const char*)"val_int");
    return 0;
  };
  String *val_str(String *)
  {
    illegal_method_call((const char*)"val_str");
    return 0;
  };
  enum Item_result result_type() const { return ROW_RESULT; }
  
  uint cols() { return item_count; }
  Item* el(uint i) { return values[i]; }
  Item** addr(uint i) { return (Item **) (values + i); }
  bool check_cols(uint c);
  bool null_inside();
  void bring_value();
  void keep_array() { save_array= 1; }
  void cleanup()
  {
    DBUG_ENTER("Item_cache_row::cleanup");
    Item_cache::cleanup();
    if (save_array)
      bzero(values, item_count*sizeof(Item**));
    else
      values= 0;
    DBUG_VOID_RETURN;
  }
};


/*
  Item_type_holder used to store type. name, length of Item for UNIONS &
  derived tables.

  Item_type_holder do not need cleanup() because its time of live limited by
  single SP/PS execution.
*/
class Item_type_holder: public Item
{
protected:
  TYPELIB *enum_set_typelib;
  enum_field_types fld_type;

  void get_full_info(Item *item);
public:
  Item_type_holder(THD*, Item*);

  Item_result result_type() const;
  virtual enum_field_types field_type() const { return fld_type; };
  enum Type type() const { return TYPE_HOLDER; }
  double val();
  longlong val_int();
  String *val_str(String*);
  bool join_types(THD *thd, Item *);
  Field *make_field_by_type(TABLE *table);
  static uint32 display_length(Item *item);
  static enum_field_types get_real_type(Item *);
};


extern Item_buff *new_Item_buff(THD *thd, Item *item);
extern Item_result item_cmp_type(Item_result a,Item_result b);
extern void resolve_const_item(THD *thd, Item **ref, Item *cmp_item);
extern bool field_is_equal_to_item(Field *field,Item *item);

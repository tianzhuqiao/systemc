#include <assert.h>
#include <time.h>
#include <cstdlib>
#include <string>
#include <iomanip>
#include <map>
#include <sstream>

#include "sysc/kernel/sc_simcontext.h"
#include "sysc/kernel/sc_ver.h"
#include "sysc/kernel/sc_event.h"
#include "sysc/datatypes/bit/sc_bit.h"
#include "sysc/datatypes/bit/sc_logic.h"
#include "sysc/datatypes/bit/sc_lv_base.h"
#include "sysc/datatypes/int/sc_signed.h"
#include "sysc/datatypes/int/sc_unsigned.h"
#include "sysc/datatypes/int/sc_int_base.h"
#include "sysc/datatypes/int/sc_uint_base.h"
#include "sysc/datatypes/fx/fx.h"
#include "sysc/bsm/sc_bsm_trace.h"
#include "sysc/utils/sc_report.h"  // sc_assert
#include "sysc/utils/sc_string.h"
#include "sysc/communication/sc_interface.h"
#include "sysc/communication/sc_port.h"
#include "sysc/communication/sc_signal_ports.h"
#include "sysc/communication/sc_signal.h"
#include "sysc/tracing/sc_trace_file_base.h"

#if defined(_MSC_VER)
# pragma warning(disable:4309)  // truncation of constant value
#endif

namespace sc_core {

// Forward declarations for functions that come later in the file
// Map sc_dt::sc_logic to printable BSM
static char map_sc_logic_state_to_bsm_state(char in_char);

// Remove name problems associated with [] in bsm names
static void remove_bsm_name_problems(std::string& name);

const char* bsm_types[bsm_trace_file::BSM_LAST] = {"wire", "real", "event", "time"};

// ----------------------------------------------------------------------------
//  CLASS : bsm_trace
//
//  Base class for BSM traces.
// ----------------------------------------------------------------------------
class bsm_trace {
 public:
     // BSM_TRACE_VAL: the current trace is a valid signal for the next trace
     // to trgger the tracing with a second signal,
     //    1) trace the valid signal
     //    2) trace the data signal (the trigger and trace setting will be ignored)
     enum { BSM_TRACE_ORIG = 0, BSM_TRACE_VAL };
     enum {
         BSM_TRIGGER_VAL_POS = 0,
         BSM_TRIGGER_VAL_NEG,
         BSM_TRIGGER_VAL_BOTH,
         BSM_TRIGGER_VAL_NONE
     };
     bsm_trace(const std::string& name_, const std::string& bsm_name_,
             const unsigned print_type_, const unsigned trigger_type_);

     // Needs to be pure virtual as has to be defined by the particular
     // type being traced
     virtual void write(FILE* f) = 0;

     virtual void set_width();

     static const char* strip_leading_bits(const char* originalbuf);

     // Comparison function needs to be pure virtual too
     virtual bool changed() = 0;

     // Make this virtual as some derived classes may overwrite
     virtual void print_variable_declaration_line(FILE* f);

     void compose_data_line(char* rawdata, char* compdata);
     std::string compose_line(const std::string data);

     virtual ~bsm_trace();

     const std::string name;
     const std::string bsm_name;
     const char* bsm_var_typ_name;
     int bit_width;

     // 0 original, 1 valid signal
     unsigned int bsm_trace_type;
     virtual void set_trace_type(unsigned int nType) {
         bsm_trace_type = nType;
     }
     virtual unsigned int get_trace_type() {
         return bsm_trace_type;
     }
     // 0 pos edge, 1 neg edge, 2 both edge, 3 none
     unsigned int bsm_trigger_type;
     virtual void set_trigger_type(unsigned int nType) {
         bsm_trigger_type = nType;
     }
     virtual unsigned int get_trigger_type() {
         return bsm_trigger_type;
     }

     unsigned int bsm_trace_print_type;
     // 0 VCD, 1 simple
     virtual void set_print_type(unsigned int nType) {
         bsm_trace_print_type = nType;
     }
     virtual unsigned int get_print_type() {
         return bsm_trace_print_type;
     }
     virtual bool is_print_vcd() {
         return bsm_trace_print_type == bsm_trace_file::BT_VCD;
     }
};


bsm_trace::bsm_trace(const std::string& name_,
        const std::string& bsm_name_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : name(name_)
    , bsm_name(bsm_name_)
    , bsm_var_typ_name(bsm_types[bsm_trace_file::BSM_WIRE])
    , bit_width(0)
    , bsm_trace_type(BSM_TRACE_ORIG)
    , bsm_trigger_type(trigger_type_)
    , bsm_trace_print_type(print_type_) {
        /* Intentionally blank */
    }

void bsm_trace::compose_data_line(char* rawdata, char* compdata) {
    assert(rawdata != compdata);

    if (bit_width == 0) {
        compdata[0] = '\0';
    } else {
        if (bit_width == 1) {
            compdata[0] = rawdata[0];
            strcpy(&(compdata[1]), bsm_name.c_str());
        } else {
            const char* effective_begin = strip_leading_bits(rawdata);
            sprintf(compdata, "b%s %s", effective_begin, bsm_name.c_str());
        }
    }
}

// same as above but not that ugly
std::string bsm_trace::compose_line(const std::string data) {
    if (bit_width == 0)
        return "";
    if (bit_width == 1)
        return data + bsm_name;
    return std::string("b") + strip_leading_bits(data.c_str()) + " " + bsm_name;
}

void bsm_trace::print_variable_declaration_line(FILE* f) {
    char buf[2000];

    if (bit_width <= 0) {
        snprintf(buf, sizeof(buf), "Traced object \"%s\" has 0 Bits, cannot be traced.",
                name.c_str());
        bsm_put_error_message(buf, false);
    } else {
        std::string namecopy = name;
        remove_bsm_name_problems(namecopy);
        if (bit_width == 1) {
            snprintf(buf, sizeof(buf), "$var %s  % 3d  %s  %s       $end\n",
                    bsm_var_typ_name,
                    bit_width,
                    bsm_name.c_str(),
                    namecopy.c_str());
        } else {
            snprintf(buf, sizeof(buf), "$var %s  % 3d  %s  %s [%d:0]  $end\n",
                    bsm_var_typ_name,
                    bit_width,
                    bsm_name.c_str(),
                    namecopy.c_str(),
                    bit_width - 1);
        }
        fputs(buf, f);
    }
}

void bsm_trace::set_width() {
    /* Intentionally Blank, should be defined for each type separately */
}

const char*
bsm_trace::strip_leading_bits(const char* originalbuf) {
    //*********************************************************************
    // - Remove multiple leading 0,z,x, and replace by only one
    // - For example,
    //    b000z100    -> b0z100
    //    b00000xxx   -> b0xxx
    //    b000        -> b0
    //    bzzzzz1     -> bz1
    //    bxxxz10     -> xz10
    // - For leading 0's followed by 1, remove all leading 0's
    //    b0000010101 -> b10101

    const char* position = originalbuf;

    if (strlen(originalbuf) < 2 ||
         (originalbuf[0] != 'z' && originalbuf[0] != 'x' && originalbuf[0] != '0'))
        return originalbuf;

    char first_char = *position;
    while (*position == first_char) {
        position++;
    }

    if (first_char == '0' && *position == '1')
        return position;
    // else
    return position - 1;
}

bsm_trace::~bsm_trace() {
    /* Intentionally Blank */
}


template <class T>
class bsm_T_trace : public bsm_trace {
 public:
     bsm_T_trace(const T& object_,
             const std::string& name_,
             const std::string& bsm_name_,
             bsm_trace_file::bsm_enum type_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH)
         : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
           , object(object_)
           , old_value(object_) {
               if (trigger_type_ == BSM_TRIGGER_VAL_NEG ||
                   trigger_type_ == BSM_TRIGGER_VAL_POS) {
                   char buf[2000];
                   snprintf(buf, sizeof(buf), "Traced object \"%s\" does not support triggering on %s edge.",
                           name.c_str(), trigger_type_ == BSM_TRIGGER_VAL_NEG ? "neg" : "pos");
                   bsm_put_error_message(buf, true);
               }
               sc_assert(type_ < bsm_trace_file::BSM_LAST);
               bsm_var_typ_name = bsm_types[type_];
           }

     void write(FILE* f) {
         if (f) {
             fprintf(f, "%s", compose_line(object.to_string()).c_str());
         }
         old_value = object;
     }

     bool changed() { return !(object == old_value); }

     void set_width() { bit_width = object.length(); }

 protected:
     const T& object;
     T        old_value;
};

typedef bsm_T_trace<sc_dt::sc_bv_base> bsm_sc_bv_trace;
typedef bsm_T_trace<sc_dt::sc_lv_base> bsm_sc_lv_trace;

// Trace sc_dt::sc_bv_base (sc_dt::sc_bv)
void bsm_trace_file::trace(
        const sc_dt::sc_bv_base& object, const std::string& name) {
    traceT(object, name);
}

// Trace sc_dt::sc_lv_base (sc_dt::sc_lv)
void bsm_trace_file::trace(
        const sc_dt::sc_lv_base& object, const std::string& name) {
    traceT(object, name);
}


/*****************************************************************************/
class bsm_sc_event_trace : public bsm_trace {
 public:
     bsm_sc_event_trace(const sc_dt::uint64& trigger_stamp_,
             const std::string& name_,
             const std::string& bsm_name_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     bool changed();

 protected:
    const sc_dt::uint64& trigger_stamp;
    sc_dt::uint64 old_trigger_stamp;
};

bsm_sc_event_trace::bsm_sc_event_trace(const sc_dt::uint64& trigger_stamp_,
                                       const std::string& name_,
                                       const std::string& bsm_name_,
                                       const unsigned print_type_,
                                       const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , trigger_stamp(trigger_stamp_), old_trigger_stamp(trigger_stamp_) {
        bsm_var_typ_name = bsm_types[bsm_trace_file::BSM_EVENT];
        bit_width = 1;
    }

bool bsm_sc_event_trace::changed() {
    return trigger_stamp != old_trigger_stamp;
}

void bsm_sc_event_trace::write(FILE* f) {
    if (!changed()) return;
    std::fprintf(f, "1%s", bsm_name.c_str());
    old_trigger_stamp = trigger_stamp;
}


/*****************************************************************************/
#define TRACE_CHANGED_IMPLEMENT() \
bool changed() {\
    if (object != old_value) {\
        if ((bsm_trigger_type == BSM_TRIGGER_VAL_BOTH) ||\
            (bsm_trigger_type == BSM_TRIGGER_VAL_POS && object > old_value) ||\
            (bsm_trigger_type == BSM_TRIGGER_VAL_NEG && object < old_value)) {\
            return true;\
        } else {\
            write(NULL);\
        }\
    }\
    return false;\
}

class bsm_bool_trace : public bsm_trace {
 public:
     bsm_bool_trace(const bool& object_,
             const std::string& name_,
             const std::string& bsm_name_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();
 protected:
     const bool& object;
     bool old_value;
};

bsm_bool_trace::bsm_bool_trace(const bool& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
      , object(object_) {
          bit_width = 1;
          old_value = object;
      }

void bsm_bool_trace::write(FILE* f) {
    if (f) {
        if (object == true) {
            fputc('1', f);
        } else {
            fputc('0', f);
        }
        if (is_print_vcd()) fprintf(f, "%s", bsm_name.c_str());
    }
    old_value = object;
}

//*****************************************************************************

class bsm_sc_bit_trace : public bsm_trace {
 public:
     bsm_sc_bit_trace(const sc_dt::sc_bit&, const std::string&,
             const std::string&,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();
 protected:
     const sc_dt::sc_bit& object;
     sc_dt::sc_bit       old_value;
};

bsm_sc_bit_trace::bsm_sc_bit_trace(const sc_dt::sc_bit& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    :bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
     , object(object_)
     , old_value(object_) {
         bit_width = 1;
     }

void bsm_sc_bit_trace::write(FILE* f) {
    if (f) {
        if (object == true) {
            fputc('1', f);
        } else {
            fputc('0', f);
        }
        if (is_print_vcd()) fprintf(f, "%s", bsm_name.c_str());
    }
    old_value = object;
}

/*****************************************************************************/
class bsm_sc_logic_trace : public bsm_trace {
 public:
     bsm_sc_logic_trace(const sc_dt::sc_logic& object_,
             const std::string& name_,
             const std::string& bsm_name_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     bool changed();

 protected:
     const sc_dt::sc_logic& object;
     sc_dt::sc_logic old_value;
};

bsm_sc_logic_trace::bsm_sc_logic_trace(const sc_dt::sc_logic& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    , old_value(object_) {
        bit_width = 1;
    }

bool bsm_sc_logic_trace::changed() {
    if (object != old_value) {
        if ((bsm_trigger_type == BSM_TRIGGER_VAL_BOTH) ||
            (bsm_trigger_type == BSM_TRIGGER_VAL_POS && object == 1 && old_value == 0) ||
            (bsm_trigger_type == BSM_TRIGGER_VAL_NEG && object == 0 && old_value == 1)) {
            return true;
        } else {
            write(NULL);
        }
    }
    return false;
}

void bsm_sc_logic_trace::write(FILE* f) {
    if (f) {
        char out_char;
        out_char = map_sc_logic_state_to_bsm_state(object.to_char());
        fputc(out_char, f);

        if (is_print_vcd()) fprintf(f, "%s", bsm_name.c_str());
    }
    old_value = object;
}


/*****************************************************************************/
class bsm_sc_unsigned_trace : public bsm_trace {
 public:
     bsm_sc_unsigned_trace(const sc_dt::sc_unsigned& object,
             const std::string& name_,
             const std::string& bsm_name_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();
     void set_width();

 protected:
     const sc_dt::sc_unsigned& object;
     sc_dt::sc_unsigned old_value;
};

bsm_sc_unsigned_trace::bsm_sc_unsigned_trace(const sc_dt::sc_unsigned& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    , old_value(object_.length()) {
        // The last may look strange, but is correct
        old_value = object;
    }

void bsm_sc_unsigned_trace::write(FILE* f) {
    if (f) {
        char rawdata[1000], *rawdata_ptr = rawdata;
        char compdata[1000];

        int bitindex;
        for (bitindex = object.length() - 1; bitindex >= 0; --bitindex) {
            *rawdata_ptr++ = "01"[(object)[bitindex]];
        }
        *rawdata_ptr = '\0';
        if (is_print_vcd()) {
            compose_data_line(rawdata, compdata);
            fputs(compdata, f);
        } else {
            fputs(rawdata, f);
        }
    }
    old_value = object;
}

void bsm_sc_unsigned_trace::set_width() {
    bit_width = object.length();
}


/*****************************************************************************/
class bsm_sc_signed_trace : public bsm_trace {
 public:
     bsm_sc_signed_trace(const sc_dt::sc_signed& object,
             const std::string& name_,
             const std::string& bsm_name_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();
     void set_width();

 protected:
     const sc_dt::sc_signed& object;
     sc_dt::sc_signed old_value;
};


bsm_sc_signed_trace::bsm_sc_signed_trace(const sc_dt::sc_signed& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    , old_value(object_.length()) {
        old_value = object;
    }

void bsm_sc_signed_trace::write(FILE* f) {
    if (f) {
        char rawdata[1000], *rawdata_ptr = rawdata;
        char compdata[1000];

        int bitindex;
        for (bitindex = object.length() - 1; bitindex >= 0; --bitindex) {
            *rawdata_ptr++ = "01"[(object)[bitindex]];
        }
        *rawdata_ptr = '\0';
        if (is_print_vcd()) {
            compose_data_line(rawdata, compdata);

            fputs(compdata, f);
        } else {
            fputs(rawdata, f);
        }
    }
    old_value = object;
}

void bsm_sc_signed_trace::set_width() {
    bit_width = object.length();
}


/*****************************************************************************/
class bsm_sc_uint_base_trace : public bsm_trace {
 public:
     bsm_sc_uint_base_trace(const sc_dt::sc_uint_base& object,
             const std::string& name_,
             const std::string& bsm_name_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();
     void set_width();

 protected:
     const sc_dt::sc_uint_base& object;
     sc_dt::sc_uint_base old_value;
};


bsm_sc_uint_base_trace::bsm_sc_uint_base_trace(
        const sc_dt::sc_uint_base& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    // initialize old_value to have same length as object
    , old_value(object_.length()) {
        old_value = object;
    }

void bsm_sc_uint_base_trace::write(FILE* f) {
    if (f) {
        char rawdata[1000], *rawdata_ptr = rawdata;
        char compdata[1000];

        int bitindex;
        for (bitindex = object.length() - 1; bitindex >= 0; --bitindex) {
            *rawdata_ptr++ = "01"[int((object)[bitindex])];
        }
        *rawdata_ptr = '\0';
        if (is_print_vcd()) {
            compose_data_line(rawdata, compdata);

            fputs(compdata, f);
        } else {
            fputs(rawdata, f);
        }
    }
    old_value = object;
}

void bsm_sc_uint_base_trace::set_width() {
    bit_width = object.length();
}


/*****************************************************************************/
class bsm_sc_int_base_trace : public bsm_trace {
 public:
     bsm_sc_int_base_trace(const sc_dt::sc_int_base& object,
             const std::string& name_,
             const std::string& bsm_name_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();
     void set_width();

 protected:
     const sc_dt::sc_int_base& object;
     sc_dt::sc_int_base old_value;
};

bsm_sc_int_base_trace::bsm_sc_int_base_trace(const sc_dt::sc_int_base& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    , old_value(object_.length()) {
        old_value = object;
    }

void bsm_sc_int_base_trace::write(FILE* f) {
    if (f) {
        char rawdata[1000], *rawdata_ptr = rawdata;
        char compdata[1000];

        int bitindex;
        for (bitindex = object.length() - 1; bitindex >= 0; --bitindex) {
            *rawdata_ptr++ = "01"[int((object)[bitindex])];
        }
        *rawdata_ptr = '\0';
        if (is_print_vcd()) {
            compose_data_line(rawdata, compdata);
            fputs(compdata, f);
        } else {
            fputs(rawdata, f);
        }
    }
    old_value = object;
}

void bsm_sc_int_base_trace::set_width() {
    bit_width = object.length();
}


/*****************************************************************************/
class bsm_sc_fxval_trace : public bsm_trace {
 public:
     bsm_sc_fxval_trace(const sc_dt::sc_fxval& object,
             const std::string& name_,
             const std::string& bsm_name_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();

 protected:
     const sc_dt::sc_fxval& object;
     sc_dt::sc_fxval old_value;
};

bsm_sc_fxval_trace::bsm_sc_fxval_trace(const sc_dt::sc_fxval& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    , old_value(object_) {
        bsm_var_typ_name = bsm_types[bsm_trace_file::BSM_REAL];
        bit_width = 1;
    }

void bsm_sc_fxval_trace::write(FILE* f) {
    if (f) {
        if (is_print_vcd())
            fprintf(f, "r%.16g %s", object.to_double(), bsm_name.c_str());
        else
            fprintf(f, "%.16g", object.to_double());
    }
    old_value = object;
}


/*****************************************************************************/
class bsm_sc_fxval_fast_trace : public bsm_trace {
 public:
     bsm_sc_fxval_fast_trace(const sc_dt::sc_fxval_fast& object,
             const std::string& name_,
             const std::string& bsm_name_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();

 protected:
     const sc_dt::sc_fxval_fast& object;
     sc_dt::sc_fxval_fast old_value;
};

bsm_sc_fxval_fast_trace::bsm_sc_fxval_fast_trace(
        const sc_dt::sc_fxval_fast& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        const unsigned print_type_,
        const unsigned trigger_type_)
        : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
        , object(object_)
        , old_value(object_) {
            bsm_var_typ_name = bsm_types[bsm_trace_file::BSM_REAL];
            bit_width = 1;
        }

void bsm_sc_fxval_fast_trace::write(FILE* f) {
    if (f) {
        if (bsm_trace_print_type == bsm_trace_file::BT_VCD)
            fprintf(f, "r%.16g %s", object.to_double(), bsm_name.c_str());
        else
            fprintf(f, "%.16g", object.to_double());
    }
    old_value = object;
}


/*****************************************************************************/
class bsm_sc_fxnum_trace : public bsm_trace {
 public:
     bsm_sc_fxnum_trace(const sc_dt::sc_fxnum& object,
             const std::string& name_,
             const std::string& bsm_name_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();
     void set_width();

 protected:
     const sc_dt::sc_fxnum& object;
     sc_dt::sc_fxnum old_value;
};

bsm_sc_fxnum_trace::bsm_sc_fxnum_trace(const sc_dt::sc_fxnum& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
      , object(object_)
      , old_value(object_.m_params.type_params(),
              object_.m_params.enc(),
              object_.m_params.cast_switch(),
              0) {
          old_value = object;
      }

void bsm_sc_fxnum_trace::write(FILE* f) {
    if (f) {
        char rawdata[1000], *rawdata_ptr = rawdata;
        char compdata[1000];

        int bitindex;
        for (bitindex = object.wl() - 1; bitindex >= 0; --bitindex)
            *rawdata_ptr++ = "01"[(object)[bitindex]];

        *rawdata_ptr = '\0';
        if (is_print_vcd()) {
            compose_data_line(rawdata, compdata);
            fputs(compdata, f);
        } else {
            fputs(rawdata, f);
        }
    }
    old_value = object;
}

void bsm_sc_fxnum_trace::set_width() {
    bit_width = object.wl();
}


/*****************************************************************************/
class bsm_sc_fxnum_fast_trace : public bsm_trace {
 public:
     bsm_sc_fxnum_fast_trace(const sc_dt::sc_fxnum_fast& object,
             const std::string& name_,
             const std::string& bsm_name_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();
     void set_width();

 protected:
     const sc_dt::sc_fxnum_fast& object;
     sc_dt::sc_fxnum_fast old_value;
};

bsm_sc_fxnum_fast_trace::bsm_sc_fxnum_fast_trace(
        const sc_dt::sc_fxnum_fast& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    , old_value(object_.m_params.type_params(),
            object_.m_params.enc(),
            object_.m_params.cast_switch(),
            0) {
        old_value = object;
    }

void bsm_sc_fxnum_fast_trace::write(FILE* f) {
    if (f) {
        char rawdata[1000], *rawdata_ptr = rawdata;
        char compdata[1000];

        int bitindex;
        for (bitindex = object.wl() - 1; bitindex >= 0; --bitindex)
            *rawdata_ptr++ = "01"[(object)[bitindex]];

        *rawdata_ptr = '\0';

        if (is_print_vcd()) {
            compose_data_line(rawdata, compdata);
            fputs(compdata, f);
        } else {
            fputs(rawdata, f);
        }
    }
    old_value = object;
}

void bsm_sc_fxnum_fast_trace::set_width() {
    bit_width = object.wl();
}


/*****************************************************************************/
class bsm_unsigned_int_trace : public bsm_trace {
 public:
     bsm_unsigned_int_trace(const unsigned& object,
             const std::string& name_,
             const std::string& bsm_name_,
             int width_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();

 protected:
     const unsigned& object;
     unsigned old_value;
     unsigned mask;
};


bsm_unsigned_int_trace::bsm_unsigned_int_trace(
        const unsigned& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        int width_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    , old_value(object_) {
        bit_width = width_;
        if (bit_width < 32) {
            mask = ~(-1 << bit_width);
        } else {
            mask = 0xffffffff;
        }
    }

void bsm_unsigned_int_trace::write(FILE* f) {
    if (f) {
        char rawdata[1000];
        char compdata[1000];
        int bitindex;

        // Check for overflow
        if ((object & mask) != object) {
            for (bitindex = 0; bitindex < bit_width; bitindex++)
                rawdata[bitindex] = 'x';
        } else {
            unsigned bit_mask = 1 << (bit_width - 1);
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = (object & bit_mask) ? '1' : '0';
                bit_mask = bit_mask >> 1;
            }
        }
        rawdata[bitindex] = '\0';
        if (is_print_vcd()) {
            compose_data_line(rawdata, compdata);
            fputs(compdata, f);
        } else {
            fputs(rawdata, f);
        }
    }
    old_value = object;
}


/*****************************************************************************/
class bsm_unsigned_short_trace : public bsm_trace {
 public:
     bsm_unsigned_short_trace(const unsigned short& object,
             const std::string& name_,
             const std::string& bsm_name_,
             int width_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();

 protected:
     const unsigned short& object;
     unsigned short old_value;
     unsigned short mask;
};

bsm_unsigned_short_trace::bsm_unsigned_short_trace(
        const unsigned short& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        int width_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    , old_value(object_) {
        bit_width = width_;
        if (bit_width < 16) {
            mask = ~(-1 << bit_width);
        } else {
            mask = 0xffff;
        }

        old_value = object;
    }

void bsm_unsigned_short_trace::write(FILE* f) {
    if (f) {
        char rawdata[1000];
        char compdata[1000];
        int bitindex;

        // Check for overflow
        if ((object & mask) != object) {
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = 'x';
            }
        } else {
            unsigned bit_mask = 1 << (bit_width - 1);
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = (object & bit_mask) ? '1' : '0';
                bit_mask = bit_mask >> 1;
            }
        }
        rawdata[bitindex] = '\0';
        if (is_print_vcd()) {
            compose_data_line(rawdata, compdata);
            fputs(compdata, f);
        } else {
            fputs(rawdata, f);
        }
    }
    old_value = object;
}


/*****************************************************************************/
class bsm_unsigned_char_trace : public bsm_trace {
 public:
     bsm_unsigned_char_trace(const unsigned char& object,
             const std::string& name_,
             const std::string& bsm_name_,
             int width_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();

 protected:
     const unsigned char& object;
     unsigned char old_value;
     unsigned char mask;
};


bsm_unsigned_char_trace::bsm_unsigned_char_trace(
        const unsigned char& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        int width_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    , old_value(object_) {
        bit_width = width_;
        if (bit_width < 8) {
            mask = ~(-1 << bit_width);
        } else {
            mask = 0xff;
        }
    }

void bsm_unsigned_char_trace::write(FILE* f) {
    if (f) {
        char rawdata[1000];
        char compdata[1000];
        int bitindex;

        // Check for overflow
        if ((object & mask) != object) {
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = 'x';
            }
        } else {
            unsigned bit_mask = 1 << (bit_width - 1);
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = (object & bit_mask) ? '1' : '0';
                bit_mask = bit_mask >> 1;
            }
        }
        rawdata[bitindex] = '\0';
        if (is_print_vcd()) {
            compose_data_line(rawdata, compdata);
            fputs(compdata, f);
        } else {
            fputs(rawdata, f);
        }
    }
    old_value = object;
}


/*****************************************************************************/
class bsm_unsigned_long_trace : public bsm_trace {
 public:
     bsm_unsigned_long_trace(const unsigned long& object,
             const std::string& name_,
             const std::string& bsm_name_,
             int width_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();

 protected:
     const unsigned long& object;
     unsigned long old_value;
     unsigned long mask;
};


bsm_unsigned_long_trace::bsm_unsigned_long_trace(
        const unsigned long& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        int width_,
        const unsigned print_type,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type, trigger_type_)
    , object(object_)
    , old_value(object_) {
        bit_width = width_;
        if (bit_width < 32) {
            mask = ~(-1 << bit_width);
        } else {
            mask = 0xffffffff;
        }
    }

void bsm_unsigned_long_trace::write(FILE* f) {
    if (f) {
        char rawdata[1000];
        char compdata[1000];
        int bitindex;

        // Check for overflow
        if ((object & mask) != object) {
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = 'x';
            }
        } else {
            unsigned long bit_mask = 1ul << (bit_width - 1);
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = (object & bit_mask) ? '1' : '0';
                bit_mask = bit_mask >> 1;
            }
        }
        rawdata[bitindex] = '\0';
        if (is_print_vcd()) {
            compose_data_line(rawdata, compdata);
            fputs(compdata, f);
        } else {
            fputs(rawdata, f);
        }
    }
    old_value = object;
}


/*****************************************************************************/
class bsm_signed_int_trace : public bsm_trace {
 public:
     bsm_signed_int_trace(const int& object,
             const std::string& name_,
             const std::string& bsm_name_,
             int width_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();

 protected:
     const int& object;
     int old_value;
     unsigned mask;
};


bsm_signed_int_trace::bsm_signed_int_trace(const signed& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        int width_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    , old_value(object_) {
        bit_width = width_;
        if (bit_width < 32) {
            mask = ~(-1 << bit_width);
        } else {
            mask = 0xffffffff;
        }
    }

void bsm_signed_int_trace::write(FILE* f) {
    if (f) {
        char rawdata[1000];
        char compdata[1000];
        int bitindex;

        // Check for overflow
        if (((unsigned)object & mask) != (unsigned)object) {
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = 'x';
            }
        } else {
            unsigned bit_mask = 1 << (bit_width - 1);
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = (object & bit_mask) ? '1' : '0';
                bit_mask = bit_mask >> 1;
            }
        }
        rawdata[bitindex] = '\0';
        if (is_print_vcd()) {
            compose_data_line(rawdata, compdata);
            fputs(compdata, f);
        } else {
            fputs(rawdata, f);
        }
    }
    old_value = object;
}


/*****************************************************************************/
class bsm_signed_short_trace : public bsm_trace {
 public:
     bsm_signed_short_trace(const short& object,
             const std::string& name_,
             const std::string& bsm_name_,
             int width_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();

 protected:
     const short& object;
     short old_value;
     unsigned short mask;
};


bsm_signed_short_trace::bsm_signed_short_trace(
        const short& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        int width_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    , old_value(object_) {
        bit_width = width_;
        if (bit_width < 16) {
            mask = ~(-1 << bit_width);
        } else {
            mask = 0xffff;
        }
    }

void bsm_signed_short_trace::write(FILE* f) {
    if (f) {
        char rawdata[1000];
        char compdata[1000];
        int bitindex;

        // Check for overflow
        if (((unsigned short)object & mask) != (unsigned short)object) {
            for (bitindex = 0; bitindex < bit_width; bitindex++)
                rawdata[bitindex] = 'x';
        } else {
            unsigned bit_mask = 1 << (bit_width - 1);
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = (object & bit_mask) ? '1' : '0';
                bit_mask = bit_mask >> 1;
            }
        }
        rawdata[bitindex] = '\0';
        if (is_print_vcd()) {
            compose_data_line(rawdata, compdata);
            fputs(compdata, f);
        } else {
            fputs(rawdata, f);
        }
    }
    old_value = object;
}


/*****************************************************************************/
class bsm_signed_char_trace : public bsm_trace {
 public:
     bsm_signed_char_trace(const char& object,
             const std::string& name_,
             const std::string& bsm_name_,
             int width_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();

 protected:
     const char& object;
     char old_value;
     unsigned char mask;
};


bsm_signed_char_trace::bsm_signed_char_trace(const char& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        int width_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    , old_value(object_) {
        bit_width = width_;
        if (bit_width < 8) {
            mask = ~(-1 << bit_width);
        } else {
            mask = 0xff;
        }
    }

void bsm_signed_char_trace::write(FILE* f) {
    if (f) {
        char rawdata[1000];
        char compdata[1000];
        int bitindex;

        // Check for overflow
        if (((unsigned char)object & mask) != (unsigned char)object) {
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = 'x';
            }
        } else {
            unsigned bit_mask = 1 << (bit_width - 1);
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = (object & bit_mask) ? '1' : '0';
                bit_mask = bit_mask >> 1;
            }
        }
        rawdata[bitindex] = '\0';
        if (is_print_vcd()) {
            compose_data_line(rawdata, compdata);
            fputs(compdata, f);
        } else {
            fputs(rawdata, f);
        }
    }
    old_value = object;
}


/*****************************************************************************/
class bsm_int64_trace : public bsm_trace {
 public:
     bsm_int64_trace(const sc_dt::int64& object,
             const std::string& name_,
             const std::string& bsm_name_,
             int width_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();

 protected:
     const sc_dt::int64& object;
     sc_dt::int64 old_value;
     sc_dt::uint64 mask;
};

bsm_int64_trace::bsm_int64_trace(const sc_dt::int64& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        int width_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    , old_value(object_) {
        bit_width = width_;
        mask = (sc_dt::uint64) - 1;
        if (bit_width < 64)  mask = ~(mask << bit_width);
    }

void bsm_int64_trace::write(FILE* f) {
    if (f) {
        char rawdata[1000];
        char compdata[1000];
        int bitindex;

        // Check for overflow
        if (((sc_dt::uint64) object & mask) != (sc_dt::uint64) object) {
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = 'x';
            }
        } else {
            sc_dt::uint64 bit_mask = 1;
            bit_mask = bit_mask << (bit_width - 1);
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = (object & bit_mask) ? '1' : '0';
                bit_mask = bit_mask >> 1;
            }
        }
        rawdata[bitindex] = '\0';
        if (is_print_vcd()) {
            compose_data_line(rawdata, compdata);
            fputs(compdata, f);
        } else {
            fputs(rawdata, f);
        }
    }
    old_value = object;
}


/*****************************************************************************/
class bsm_uint64_trace : public bsm_trace {
 public:
     bsm_uint64_trace(const sc_dt::uint64& object,
             const std::string& name_,
             const std::string& bsm_name_,
             int width_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();

 protected:
     const sc_dt::uint64& object;
     sc_dt::uint64 old_value;
     sc_dt::uint64 mask;
};


bsm_uint64_trace::bsm_uint64_trace(const sc_dt::uint64& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        int width_,
        const unsigned print_type_,
        const unsigned trigger_type_)
        : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
        , object(object_)
        , old_value(object_) {
            bit_width = width_;
            mask = (sc_dt::uint64) - 1;
            if (bit_width < 64) mask = ~(mask << bit_width);
        }

void bsm_uint64_trace::write(FILE* f) {
    if (f) {
        char rawdata[1000];
        char compdata[1000];
        int bitindex;

        // Check for overflow
        if ((object & mask) != object) {
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = 'x';
            }
        } else {
            sc_dt::uint64 bit_mask = 1;
            bit_mask = bit_mask << (bit_width - 1);
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = (object & bit_mask) ? '1' : '0';
                bit_mask = bit_mask >> 1;
            }
        }
        rawdata[bitindex] = '\0';
        if (is_print_vcd()) {
            compose_data_line(rawdata, compdata);
            fputs(compdata, f);
        } else {
            fputs(rawdata, f);
        }
    }
    old_value = object;
}

/*****************************************************************************/
class bsm_sc_time_trace : public bsm_uint64_trace {
 public:
     bsm_sc_time_trace(const sc_time& object_,
             const std::string& name_,
             const std::string& bsm_name_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);

     bool changed();

     const sc_time& object;
     sc_dt::uint64 shadow_object;  // trace raw value internally
};

bsm_sc_time_trace::bsm_sc_time_trace(const sc_time& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    // initialize shadow_object before passing its reference to base class
    : bsm_uint64_trace((shadow_object = object_.value()), name_, bsm_name_, 64,
                       print_type_, trigger_type_)
    , object(object_) {
        bsm_var_typ_name = bsm_types[bsm_trace_file::BSM_TIME];
    }

bool bsm_sc_time_trace::changed() {
    shadow_object = object.value();
    return bsm_uint64_trace::changed();
}


/*****************************************************************************/
class bsm_signed_long_trace : public bsm_trace {
 public:
     bsm_signed_long_trace(const long& object,
             const std::string& name_,
             const std::string& bsm_name_,
             int width_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();

 protected:
     const long& object;
     long old_value;
     unsigned long mask;
};

bsm_signed_long_trace::bsm_signed_long_trace(const long& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        int width_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    , old_value(object_) {
        bit_width = width_;
        if (bit_width < 32) {
            mask = ~(-1 << bit_width);
        } else {
            mask = 0xffffffff;
        }
    }

void bsm_signed_long_trace::write(FILE* f) {
    if (f) {
        char rawdata[1000];
        char compdata[1000];
        int bitindex;

        // Check for overflow
        if (((unsigned long)object & mask) != (unsigned long)object) {
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = 'x';
            }
        } else {
            unsigned long bit_mask = 1ul << (bit_width - 1);
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = (object & bit_mask) ? '1' : '0';
                bit_mask = bit_mask >> 1;
            }
        }
        rawdata[bitindex] = '\0';
        if (is_print_vcd()) {
            compose_data_line(rawdata, compdata);
            fputs(compdata, f);
        } else {
            fputs(rawdata, f);
        }
    }
    old_value = object;
}


/*****************************************************************************/
class bsm_float_trace : public bsm_trace {
 public:
     bsm_float_trace(const float& object,
             const std::string& name_,
             const std::string& bsm_name_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();

 protected:
     const float& object;
     float old_value;
};

bsm_float_trace::bsm_float_trace(const float& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    , old_value(object_) {
        bsm_var_typ_name = bsm_types[bsm_trace_file::BSM_REAL];
        bit_width = 1;
    }

void bsm_float_trace::write(FILE* f) {
    if (f) {
        if (is_print_vcd())
            fprintf(f, "r%.16g %s", object, bsm_name.c_str());
        else
            fprintf(f, "%.16g", object);
    }
    old_value = object;
}


/*****************************************************************************/
class bsm_double_trace : public bsm_trace {
 public:
     bsm_double_trace(const double& object,
             const std::string& name_,
             const std::string& bsm_name_,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();

 protected:
     const double& object;
     double old_value;
};

bsm_double_trace::bsm_double_trace(const double& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    , old_value(object_) {
        bsm_var_typ_name = bsm_types[bsm_trace_file::BSM_REAL];
        bit_width = 1;
    }

void bsm_double_trace::write(FILE* f) {
    if (f) {
        if (is_print_vcd())
            fprintf(f, "r%.16g %s", object, bsm_name.c_str());
        else
            fprintf(f, "%.16g", object);
    }
    old_value = object;
}


/*****************************************************************************/
class bsm_enum_trace : public bsm_trace {
 public:
     bsm_enum_trace(const unsigned& object_,
             const std::string& name_,
             const std::string& bsm_name_,
             const char** enum_literals,
             const unsigned print_type_,
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     TRACE_CHANGED_IMPLEMENT();

 protected:
     const unsigned& object;
     unsigned old_value;
     unsigned mask;
     const char** literals;
     unsigned nliterals;
};


bsm_enum_trace::bsm_enum_trace(const unsigned& object_,
        const std::string& name_,
        const std::string& bsm_name_,
        const char** enum_literals_,
        const unsigned print_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    , old_value(object_)
    , literals(enum_literals_) {
        // find number of bits required to represent enumeration literal - counting loop
        for (nliterals = 0; enum_literals_[nliterals]; nliterals++) {}

        // Figure out number of bits required to represent the number of literals
        bit_width = 0;
        unsigned shifted_maxindex = nliterals - 1;
        while (shifted_maxindex != 0) {
            shifted_maxindex >>= 1;
            bit_width++;
        }

        // Set the mask
        if (bit_width < 32) {
            mask = ~(-1 << bit_width);
        } else {
            mask = 0xffffffff;
        }
    }

void bsm_enum_trace::write(FILE* f) {
    if (f) {
        char rawdata[1000];
        char compdata[1000];
        int bitindex;

        // Check for overflow
        if ((object & mask) != object) {
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = 'x';
            }
        } else {
            unsigned bit_mask = 1 << (bit_width - 1);
            for (bitindex = 0; bitindex < bit_width; bitindex++) {
                rawdata[bitindex] = (object & bit_mask) ? '1' : '0';
                bit_mask = bit_mask >> 1;
            }
        }
        rawdata[bitindex] = '\0';
        if (is_print_vcd()) {
            compose_data_line(rawdata, compdata);
            fputs(compdata, f);
        } else {
            fputs(rawdata, f);
        }
    }
    old_value = object;
}


/*****************************************************************************
  bsm_trace_file functions
 *****************************************************************************/
bsm_trace_file::bsm_trace_file(const char *name, unsigned int type)
    : sc_trace_file_base(name, "bsm")
    , bsm_name_index(0)
    , previous_time_units_low(0)
    , previous_time_units_high(0)
    , traces() {
        bsm_print_type = type;
        bsm_trace_enable = true;
    }

void bsm_trace_file::do_initialize() {
    if (bsm_print_type == BT_VCD) {
        // date:
        fprintf(fp, "$date\n     %s\n$end\n\n", localtime_string().c_str());

        // version:
        fprintf(fp, "$version\n %s\n$end\n\n", sc_version());

        // timescale
        fprintf(fp, "$timescale\n     %s\n$end\n\n", fs_unit_to_str(trace_unit_fs).c_str());

        // Create a dummy scope
        fputs("$scope module SystemC $end\n", fp);

        // variable definitions:
        int i;
        for (i = 0; i < (int)traces.size(); i++) {
            bsm_trace* t = traces[i];
            t->set_width();  // needed for all vectors
            if (t->get_trace_type() == bsm_trace::BSM_TRACE_VAL) {
                // ignore the "valid" type signal
                continue;
            }
            t->print_variable_declaration_line(fp);
        }

        fputs("$upscope $end\n", fp);

        fputs("$enddefinitions  $end\n\n", fp);

        timestamp_in_trace_units(previous_time_units_high, previous_time_units_low);

        std::stringstream ss;

        ss << "All initial values are dumped below at time "
            << sc_time_stamp().to_seconds() <<" sec = ";
        if (has_low_units())
            ss << previous_time_units_high << std::setfill('0') << std::setw(low_units_len()) << previous_time_units_low;
        else
            ss << previous_time_units_high;
        ss << " timescale units.";

        write_comment(ss.str());

        fputs("$dumpvars\n", fp);
        for (i = 0; i < (int)traces.size(); i++) {
            bsm_trace* t = traces[i];
            if (t->get_trace_type() == bsm_trace::BSM_TRACE_VAL) {
                // ignore the "valid" type signal
                continue;
            }
            t->write(fp);
            fputc('\n', fp);
        }
        fputs("$end\n\n", fp);
    } else {
        // variable definitions:
        int i;
        for (i = 0; i < (int)traces.size(); i++) {
            bsm_trace* t = traces[i];
            t->set_width();  // needed for all vectors
        }

        timestamp_in_trace_units(previous_time_units_high, previous_time_units_low);

        for (i = 0; i < (int)traces.size(); i++) {
            bsm_trace* t = traces[i];
            if (t->get_trace_type() == bsm_trace::BSM_TRACE_ORIG) {
                t->write(fp);
                fputc('\n', fp);
            } else {
                t->write(NULL);
            }
        }
    }
}

#if SC_TRACING_PHASE_CALLBACKS_
void bsm_trace_file::trace(sc_trace_file*) const {
    SC_REPORT_ERROR(sc_core::SC_ID_INTERNAL_ERROR_,
                    "invalid call to bsm_trace_file::trace(sc_trace_file*)");
}
#endif  // SC_TRACING_PHASE_CALLBACKS_
        //
// ----------------------------------------------------------------------------
#define DEFN_TRACE_METHOD(tp)                                                 \
void                                                                          \
bsm_trace_file::trace(const tp& object_, const std::string& name_) {          \
    if (add_trace_check(name_)) {                                             \
        traces.push_back(new bsm_ ## tp ## _trace(extract_ref(object_),                    \
                                                  name_,                      \
                                                  obtain_name(),              \
                                                  bsm_print_type));           \
    }                                                                         \
}

DEFN_TRACE_METHOD(sc_event)
DEFN_TRACE_METHOD(sc_time)

DEFN_TRACE_METHOD(bool)
DEFN_TRACE_METHOD(float)
DEFN_TRACE_METHOD(double)

#undef DEFN_TRACE_METHOD
#define DEFN_TRACE_METHOD(tp)                                                 \
void                                                                          \
bsm_trace_file::trace(const sc_dt::tp& object_, const std::string& name_) {   \
    if (add_trace_check(name_)) {                                             \
        traces.push_back(new bsm_ ## tp ## _trace(object_,                    \
                                                  name_,                      \
                                                  obtain_name(),                \
                                                  bsm_print_type));           \
    }                                                                         \
}

DEFN_TRACE_METHOD(sc_bit)
DEFN_TRACE_METHOD(sc_logic)

DEFN_TRACE_METHOD(sc_signed)
DEFN_TRACE_METHOD(sc_unsigned)
DEFN_TRACE_METHOD(sc_int_base)
DEFN_TRACE_METHOD(sc_uint_base)

DEFN_TRACE_METHOD(sc_fxval)
DEFN_TRACE_METHOD(sc_fxval_fast)
DEFN_TRACE_METHOD(sc_fxnum)
DEFN_TRACE_METHOD(sc_fxnum_fast)

#undef DEFN_TRACE_METHOD


#define DEFN_TRACE_METHOD_SIGNED(tp)                                          \
void                                                                          \
bsm_trace_file::trace(const tp&        object_,                               \
                      const std::string& name_,                               \
                       int              width_) {                             \
    if (add_trace_check(name_)) {                                             \
        traces.push_back(new bsm_signed_ ## tp ## _trace(object_,             \
                                                         name_,               \
                                                         obtain_name(),       \
                                                         width_,              \
                                                         bsm_print_type));    \
    }                                                                         \
}

#define DEFN_TRACE_METHOD_UNSIGNED(tp)                                        \
void                                                                          \
bsm_trace_file::trace(const unsigned tp& object_,                             \
                      const std::string&   name_,                             \
                      int                width_) {                            \
    if (add_trace_check(name_)) {                                             \
        traces.push_back(new bsm_unsigned_ ## tp ## _trace(object_,           \
                                                           name_,             \
                                                           obtain_name(),     \
                                                           width_,            \
                                                           bsm_print_type));  \
    }                                                                         \
}

DEFN_TRACE_METHOD_SIGNED(char)
DEFN_TRACE_METHOD_SIGNED(short)
DEFN_TRACE_METHOD_SIGNED(int)
DEFN_TRACE_METHOD_SIGNED(long)

DEFN_TRACE_METHOD_UNSIGNED(char)
DEFN_TRACE_METHOD_UNSIGNED(short)
DEFN_TRACE_METHOD_UNSIGNED(int)
DEFN_TRACE_METHOD_UNSIGNED(long)

#undef DEFN_TRACE_METHOD_SIGNED
#undef DEFN_TRACE_METHOD_UNSIGNED

#define DEFN_TRACE_METHOD_LONG_LONG(tp)                                       \
void bsm_trace_file::trace(const sc_dt::tp&   object_,                        \
                           const std::string& name_,                          \
                           int                width_) {                       \
    if (add_trace_check(name_)) {                                             \
        traces.push_back(new bsm_ ## tp ## _trace(object_,                    \
                                                  name_,                      \
                                                  obtain_name(),              \
                                                  width_,                     \
                                                  bsm_print_type));           \
    }                                                                         \
}

DEFN_TRACE_METHOD_LONG_LONG(int64)
DEFN_TRACE_METHOD_LONG_LONG(uint64)

#undef DEFN_TRACE_METHOD_LONG_LONG

void bsm_trace_file::trace(const unsigned& object_,
        const std::string& name_,
        const char** enum_literals_) {
    if (add_trace_check(name_)) {
        traces.push_back(new bsm_enum_trace(object_,
                                            name_,
                                            obtain_name(),
                                            enum_literals_,
                                            bsm_print_type));
    }
}

void bsm_trace_file::write_comment(const std::string& comment) {
    if (!fp) open_fp();
    // no newline in comments allowed, as some viewers may crash
    fputs("$comment\n", fp);
    fputs(comment.c_str(), fp);
    fputs("\n$end\n\n", fp);
}

void bsm_trace_file::cycle(bool this_is_a_delta_cycle) {
    // Trace delta cycles only when enabled
    if (!delta_cycles() && this_is_a_delta_cycle) return;

    // Check for initialization
    if (initialize()) {
        return;
    }

    unit_type now_units_high, now_units_low;

    bool time_advanced = get_time_stamp(now_units_high, now_units_low);

    if (!has_low_units() && (now_units_low != 0)) {
        std::stringstream ss;
        ss << "\n\tCurrent kernel time is " << sc_time_stamp();
        ss << "\n\tVCD trace time unit is " << fs_unit_to_str(trace_unit_fs);
        ss << "\n\tUse 'tracefile->set_time_unit(double, sc_time_unit);' to increase the time resolution.";
        SC_REPORT_WARNING(SC_ID_TRACING_VCD_TIME_RESOLUTION_, ss.str().c_str());
    }

    if (delta_cycles()) {
        if (this_is_a_delta_cycle) {
            static bool warned = false;
            if (!warned) {
                SC_REPORT_INFO(SC_ID_TRACING_VCD_DELTA_CYCLE_,
                               fs_unit_to_str(trace_unit_fs).c_str());
                warned = true;
            }

            if (sc_delta_count_at_current_time() == 0) {
                if (!time_advanced) {
                    std::stringstream ss;
                    ss <<"\n\tThis can occur when delta cycle tracing is activated."
                       <<"\n\tSome delta cycles at " << sc_time_stamp() << " are not shown in vcd."
                       <<"\n\tUse 'tracefile->set_time_unit(double, sc_time_unit);' to increase the time resolution.";
                    SC_REPORT_WARNING(SC_ID_TRACING_REVERSED_TIME_, ss.str().c_str());

                    return;
                }
            }
        }

        if (!this_is_a_delta_cycle) {
            if (time_advanced) {
                previous_time_units_high = now_units_high;
                previous_time_units_low = now_units_low;
            }
            // Value updates can't happen during timed notification
            // so it is safe to skip printing
            return;
        }
    }

    // Now do the actual printing
    bool time_printed = false;
    bsm_trace* const* const l_traces = &traces[0];
    for (int i = 0; i < (int)traces.size(); i++) {
        bsm_trace* t = l_traces[i];
        if (t->changed()) {
            if (!bsm_trace_enable) {
                t->write(NULL);
            } else {
                if (!time_printed) {
                    if (bsm_print_type == BT_VCD) {
                        print_time_stamp(now_units_high, now_units_low);

                        time_printed = true;
                    }
                }

                // Write the variable
                if (t->get_trace_type() == bsm_trace::BSM_TRACE_ORIG) {
                    t->write(fp);
                } else {
                    t->write(NULL);
                    i++;
                    assert(i < (int)traces.size());
                    l_traces[i]->write(fp);
                }
                fputc('\n', fp);
            }
        }
    }
    // Put another newline after all values are printed
    if (time_printed) fputc('\n', fp);
}

bool bsm_trace_file::get_time_stamp(sc_trace_file_base::unit_type &now_units_high,
                                    sc_trace_file_base::unit_type &now_units_low) const {
    timestamp_in_trace_units(now_units_high, now_units_low);

    return ( (now_units_low > previous_time_units_low && now_units_high == previous_time_units_high)
            || now_units_high > previous_time_units_high);
}

void bsm_trace_file::print_time_stamp(sc_trace_file_base::unit_type now_units_high,
                                      sc_trace_file_base::unit_type now_units_low) const {
    std::stringstream ss;
    if (has_low_units())
        ss << "#" << now_units_high << std::setfill('0') << std::setw(low_units_len()) << now_units_low;
    else
        ss << "#" << now_units_high;

    fputs(ss.str().c_str(), fp);
    fputc('\n', fp);
}


// Create a BSM name for a variable
std::string
bsm_trace_file::obtain_name() {
    const char first_type_used = 'a';
    const int used_types_count = 'z' - 'a' + 1;
    int result;

    result = bsm_name_index;
    char char6 = (char)(bsm_name_index % used_types_count);

    result = result / used_types_count;
    char char5 = (char)(result % used_types_count);

    result = result / used_types_count;
    char char4 = (char)(result % used_types_count);

    result = bsm_name_index / used_types_count;
    char char3 = (char)(result % used_types_count);

    result = result / used_types_count;
    char char2 = (char)(result % used_types_count);

    char buf[20];
    snprintf(buf, sizeof(buf), "%c%c%c%c%c",
            char2 + first_type_used,
            char3 + first_type_used,
            char4 + first_type_used,
            char5 + first_type_used,
            char6 + first_type_used);
    bsm_name_index++;
    return std::string(buf);
}

bsm_trace_file::~bsm_trace_file() {
    unit_type now_units_high, now_units_low;
    if (is_initialized() && get_time_stamp(now_units_high, now_units_low)) {
        print_time_stamp(now_units_high, now_units_low);
    }

    for (int i = 0; i < (int)traces.size(); i++) {
        bsm_trace* t = traces[i];
        delete t;
    }
}

void bsm_trace_file::set_bsm_trace_type(int index,
        unsigned int nTrigger,
        unsigned int nTrace) {
    if (index == -1)
        index = traces.size() - 1;

    assert(index >= 0 && index < (int)traces.size());
    traces[index]->set_trace_type(nTrace);
    traces[index]->set_trigger_type(nTrigger);
}

void bsm_trace_file::set_bsm_print_type(unsigned int type) {
    bsm_print_type = type;
    int i;
    for (i = 0; i < (int)traces.size(); i++) {
        bsm_trace* t = traces[i];
        t->set_print_type(bsm_print_type);
    }
}

void  bsm_trace_file::enable_bsm_trace(bool bEnable /*= true*/) {
    bsm_trace_enable = bEnable;
}


// Functions specific to BSM tracing
static char map_sc_logic_state_to_bsm_state(char in_char) {
    char out_char;

    switch (in_char) {
        case 'U':
        case 'X':
        case 'W':
        case 'D':
            out_char = 'x';
            break;
        case '0':
        case 'L':
            out_char = '0';
            break;
        case  '1':
        case  'H':
            out_char = '1';
            break;
        case  'Z':
            out_char = 'z';
            break;
        default:
            out_char = '?';
        }

        return out_char;
    }


void bsm_put_error_message(const char* msg, bool just_warning) {
    if (just_warning) {
        ::std::cout << "BSM Trace Warning:\n" << msg << "\n" << ::std::endl;
    } else {
        ::std::cout << "BSM Trace ERROR:\n" << msg << "\n" << ::std::endl;
    }
}


static void remove_bsm_name_problems(std::string& name) {
    char message[4000];
    static bool warned = false;

    bool braces_removed = false;
    for (unsigned int i = 0; i < name.length(); i++) {
        if (name[i] == '[') {
            name[i] = '(';
            braces_removed = true;
        } else if (name[i] == ']') {
            name[i] = ')';
            braces_removed = true;
            }
        }

    if (braces_removed && !warned) {
        snprintf(message, sizeof(message),
                "Traced objects found with name containing [], which may be\n"
                "interpreted by the waveform viewer in unexpected ways.\n"
                "So the [] is automatically replaced by ().");
        bsm_put_error_message(message, true);
        warned = true;
    }
}


/*****************************************************************************/
class bsm_interface_trace : public bsm_trace {
 public:
     bsm_interface_trace(const sc_interface* object_,
             const std::string& name_,
             const std::string& bsm_name_,
             const unsigned print_type_,
             const char data_type_='r',
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     bool changed();

     void SetDataType(char type) {
        data_type = type;
     }
 protected:
     const sc_interface* object;
     std::string old_value;
     char data_type;
};

bsm_interface_trace::bsm_interface_trace(const sc_interface* object_,
        const std::string& name_,
        const std::string& bsm_name_,
        const unsigned print_type_,
        const char data_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    , data_type(data_type_) {
        bit_width = 1;
        old_value = object->bsm_string();
    }

bool bsm_interface_trace::changed() {
    std::string value = object->bsm_string();
    if (value != old_value) {
        if ((bsm_trigger_type == BSM_TRIGGER_VAL_BOTH) ||
                (bsm_trigger_type == BSM_TRIGGER_VAL_POS&&value > old_value) ||
                (bsm_trigger_type == BSM_TRIGGER_VAL_NEG&&value < old_value)) {
            return true;
        } else {
            write(NULL);
        }
    }
    return false;
}

void bsm_interface_trace::write(FILE* f) {
    std::string value = object->bsm_string();
    if (f) {
        if (is_print_vcd()) {
            if (data_type == 'b') {
                fprintf(f, "%c%s %s", data_type, strip_leading_bits(value.c_str()), bsm_name.c_str());
            } else {
                fprintf(f, "%c%s %s", data_type, value.c_str(), bsm_name.c_str());
            }
        } else {
            fprintf(f, "%s", value.c_str());
        }
    }
    old_value = value;
}

void bsm_trace_file::trace(const sc_interface* object_,
        const std::string& name_, const char data_type) {
    if (add_trace_check(name_)) {
        traces.push_back(new bsm_interface_trace(object_,
                                                 name_,
                                                 obtain_name(),
                                                 bsm_print_type,
                                                 data_type));
    }
}


/*****************************************************************************/
class bsm_port_trace : public bsm_trace {
 public:
     bsm_port_trace(const sc_port_base* object_,
             const std::string& name_,
             const std::string& bsm_name_,
             const unsigned print_type_,
             const char data_type='r',
             const unsigned trigger_type_ = BSM_TRIGGER_VAL_BOTH);
     void write(FILE* f);
     bool changed();

 protected:
     const sc_port_base* object;
     std::string old_value;
     char data_type;
};

bsm_port_trace::bsm_port_trace(const sc_port_base* object_,
        const std::string& name_,
        const std::string& bsm_name_,
        const unsigned print_type_,
        const char data_type_,
        const unsigned trigger_type_)
    : bsm_trace(name_, bsm_name_, print_type_, trigger_type_)
    , object(object_)
    , data_type(data_type_) {
        bit_width = 1;
        old_value = object->bsm_string();
    }

bool bsm_port_trace::changed() {
    std::string value = object->bsm_string();

    if (value != old_value) {
        if ((bsm_trigger_type == BSM_TRIGGER_VAL_BOTH) ||
                (bsm_trigger_type == BSM_TRIGGER_VAL_POS&&value > old_value) ||
                (bsm_trigger_type == BSM_TRIGGER_VAL_NEG&&value < old_value)) {
            return true;
        } else {
            write(NULL);
        }
    }
    return false;
}

void bsm_port_trace::write(FILE* f) {
    std::string value = object->bsm_string();
    if (f) {
        if (bsm_trace_print_type == bsm_trace_file::BT_VCD) {
            if (data_type == 'b') {
                fprintf(f, "%c%s %s", data_type, strip_leading_bits(value.c_str()), bsm_name.c_str());
            } else {
                fprintf(f, "%c%s %s", data_type, value.c_str(), bsm_name.c_str());
            }
        } else {
            fprintf(f, "%s", value.c_str());
        }
    }
    old_value = value;
}

void bsm_trace_file::trace(const sc_port_base* object_,
        const std::string& name_, const char data_type) {
    if (add_trace_check(name_)) {
        traces.push_back(new bsm_port_trace(object_,
                                            name_,
                                            obtain_name(),
                                            bsm_print_type,
                                            data_type));
    }
}

sc_trace_file* sc_create_bsm_trace_file(const char * name, unsigned int type) {
    sc_trace_file *tf = new bsm_trace_file(name, type);
    return tf;
}

void sc_close_bsm_trace_file(sc_trace_file* tf) {
    bsm_trace_file* bsm_tf = static_cast<bsm_trace_file*>(tf);
    delete bsm_tf;
}

/////////////////////////////////////////////////////////
//////////// trace bsm object ///////////////////////////
bool bsm_trace_signal(bsm_trace_file*tf, sc_object* scObj) {
    sc_interface* interf = dynamic_cast<sc_interface*> (scObj);
    if (interf == NULL)
        return false;

    std::string strBSMType = interf->bsm_type();
#define  BSM_CHECK_TYPE(type) dynamic_cast< sc_signal<type >* >(scObj) != NULL
#define  BSM_TRACE_TYPE(type) \
    sc_signal<type > *dyObj = dynamic_cast<sc_signal<type >* >(scObj); \
    assert(dyObj);\
    sc_trace(tf, *dyObj, dyObj->name());\
    return true;

    if (strBSMType.compare("Generic") == 0) {
        if (BSM_CHECK_TYPE(double)) {
            BSM_TRACE_TYPE(double)
        } else if (BSM_CHECK_TYPE(float)) {
            BSM_TRACE_TYPE(float)
        } else if (BSM_CHECK_TYPE(bool)) {
            BSM_TRACE_TYPE(bool)
        } else if (BSM_CHECK_TYPE(char)) {
            sc_signal<char > *dyObj =
                dynamic_cast<sc_signal<char >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(short)) {
            sc_signal<short > *dyObj =
                dynamic_cast<sc_signal<short >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(int)) {
            sc_signal<int > *dyObj =
                dynamic_cast<sc_signal<int >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(long)) {
            sc_signal<long > *dyObj =
                dynamic_cast<sc_signal<long >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(long long)) {
            sc_signal<long long > *dyObj =
                dynamic_cast<sc_signal<long long >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(unsigned char)) {
            sc_signal<unsigned char > *dyObj =
                dynamic_cast<sc_signal<unsigned char >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(unsigned short)) {
            sc_signal<unsigned short > *dyObj =
                dynamic_cast<sc_signal<unsigned short >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(unsigned int)) {
            sc_signal<unsigned int > *dyObj =
                dynamic_cast<sc_signal<unsigned int >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(unsigned long)) {
            sc_signal<unsigned long > *dyObj =
                dynamic_cast<sc_signal<unsigned long >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(unsigned long long)) {
            sc_signal<unsigned long long > *dyObj =
                dynamic_cast<sc_signal<unsigned long long>*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        }
        //  else if (BSM_CHECK_TYPE(std::string))
        //  {  // std::string
        //      sc_signal<std::string > *dyObj =
        //          dynamic_cast< sc_signal<std::string >* >(scObj);
        //      ASSERT(dyObj);
        //      sc_trace(tf,*dyObj,dyObj->name());
        // .    return true;
        //  }
        else if (BSM_CHECK_TYPE(sc_dt::sc_logic)) {
            sc_signal<sc_dt::sc_logic > *dyObj =
                dynamic_cast<sc_signal<sc_dt::sc_logic >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(sc_dt::sc_bit)) {
            sc_signal<sc_dt::sc_bit > *dyObj =
                dynamic_cast<sc_signal<sc_dt::sc_bit >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        }
#undef BSM_CHECK_TYPE
    } else {
        if (strBSMType.compare(("sc_int")) == 0 ||
                strBSMType.compare(("sc_uint")) == 0 ||
                strBSMType.compare(("sc_bigint")) == 0 ||
                strBSMType.compare(("sc_biguint")) == 0 ||
                strBSMType.compare(("sc_fixed")) == 0 ||
                strBSMType.compare(("sc_fixed_fast")) == 0 ||
                strBSMType.compare(("sc_ufixed")) == 0) {
            tf->trace(interf, scObj->name(), 'r');
            return true;
        } else if (strBSMType.compare(("sc_bv")) == 0  ||
                strBSMType.compare(("sc_lv")) == 0) {
            tf->trace(interf, scObj->name(), 'b');
            return true;
        }
    }
    return false;
}

bool bsm_trace_in(bsm_trace_file*tf, sc_object* scObj) {
    sc_port_base* interf = dynamic_cast<sc_port_base*> (scObj);
    if (interf == NULL)
        return false;

    std::string strBSMType = interf->bsm_type();
    if (strBSMType.compare("Generic") == 0) {
#define  BSM_CHECK_TYPE(type) dynamic_cast< sc_in<type >* >(scObj) != NULL
        if (BSM_CHECK_TYPE(double)) {
            sc_in<double > *dyObj =
                dynamic_cast<sc_in<double >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(float)) {
            sc_in<float > *dyObj =
                dynamic_cast<sc_in<float >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(bool)) {
            sc_in<bool > *dyObj =
                dynamic_cast<sc_in<bool >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(char)) {
            sc_in<char > *dyObj =
                dynamic_cast<sc_in<char >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(short)) {
            sc_in<short > *dyObj =
                dynamic_cast<sc_in<short >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(int)) {
            sc_in<int > *dyObj =
                dynamic_cast<sc_in<int >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(long)) {
            sc_in<long > *dyObj =
                dynamic_cast<sc_in<long >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(long long)) {
            sc_in<long long > *dyObj =
                dynamic_cast<sc_in<long long >*>(scObj);
            sc_trace(tf, *dyObj, dyObj->name());
            assert(dyObj);
            return true;
        } else if (BSM_CHECK_TYPE(unsigned char)) {
            sc_in<unsigned char > *dyObj =
                dynamic_cast<sc_in<unsigned char >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(unsigned short)) {
            sc_in<unsigned short > *dyObj =
                dynamic_cast<sc_in<unsigned short >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(unsigned int)) {
            sc_in<unsigned int > *dyObj =
                dynamic_cast<sc_in<unsigned int >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(unsigned long)) {
            sc_in<unsigned long > *dyObj =
                dynamic_cast<sc_in<unsigned long >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(unsigned long long)) {
            sc_in<unsigned long long > *dyObj =
                dynamic_cast<sc_in<unsigned long long>*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        }
        //  else if (BSM_CHECK_TYPE(std::string))
        //  {  // std::string
        //      sc_signal<std::string > *dyObj =
        //          dynamic_cast< sc_signal<std::string >* >(scObj);
        //      ASSERT(dyObj);
        //      sc_trace(tf,*dyObj,dyObj->name());
        //      return true;
        //  }
        else if (BSM_CHECK_TYPE(sc_dt::sc_logic)) {
            sc_in<sc_dt::sc_logic > *dyObj =
                dynamic_cast<sc_in<sc_dt::sc_logic >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(sc_dt::sc_bit)) {
            sc_in<sc_dt::sc_bit > *dyObj =
                dynamic_cast<sc_in<sc_dt::sc_bit >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        }
#undef BSM_CHECK_TYPE
    } else {
        if (strBSMType.compare(("sc_int")) == 0 ||
                strBSMType.compare(("sc_uint")) == 0 ||
                strBSMType.compare(("sc_bigint")) == 0 ||
                strBSMType.compare(("sc_biguint")) == 0 ||
                strBSMType.compare(("sc_fixed")) == 0 ||
                strBSMType.compare(("sc_fixed_fast")) == 0 ||
                strBSMType.compare(("sc_ufixed")) == 0) {
             tf->trace(interf, scObj->name(), 'r');
             return true;
        } else if (strBSMType.compare(("sc_bv")) == 0 ||
                strBSMType.compare(("sc_lv")) == 0) {
            tf->trace(interf, scObj->name(), 'b');
            return true;
        }
    }
    return false;
}

bool bsm_trace_out(bsm_trace_file*tf, sc_object* scObj) {
    sc_port_base* interf = dynamic_cast<sc_port_base*> (scObj);
    if (interf == NULL)
        return false;

    std::string strBSMType = interf->bsm_type();
    if (strBSMType.compare("Generic") == 0) {
#define  BSM_CHECK_TYPE(type) dynamic_cast< sc_inout<type >* >(scObj) != NULL
        if (BSM_CHECK_TYPE(double)) {
            sc_inout<double > *dyObj = dynamic_cast<sc_inout<double >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(float)) {
            sc_inout<float > *dyObj = dynamic_cast<sc_inout<float >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(bool)) {
            sc_inout<bool > *dyObj = dynamic_cast<sc_inout<bool >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(char)) {
            sc_inout<char > *dyObj = dynamic_cast<sc_inout<char >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(short)) {
            sc_inout<short > *dyObj =
                dynamic_cast<sc_inout<short >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(int)) {
            sc_inout<int > *dyObj = dynamic_cast<sc_inout<int >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(long)) {
            sc_inout<long > *dyObj = dynamic_cast<sc_inout<long >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(long long)) {
            sc_inout<long long > *dyObj =
                dynamic_cast<sc_inout<long long >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(unsigned char)) {
            sc_inout<unsigned char > *dyObj =
                dynamic_cast<sc_inout<unsigned char >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(unsigned short)) {
            sc_inout<unsigned short > *dyObj =
                dynamic_cast<sc_inout<unsigned short >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(unsigned int)) {
            sc_inout<unsigned int > *dyObj =
                dynamic_cast<sc_inout<unsigned int >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(unsigned long)) {
            sc_inout<unsigned long > *dyObj =
                dynamic_cast<sc_inout<unsigned long >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(unsigned long long)) {
            sc_inout<unsigned long long > *dyObj =
                dynamic_cast<sc_inout<unsigned long long>*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        }
        //  else if (BSM_CHECK_TYPE(std::string))
        //  {   // std::string
        //      sc_signal<std::string > *dyObj =
        //          dynamic_cast< sc_signal<std::string >* >(scObj);
        //      ASSERT(dyObj);
        //      sc_trace(tf,*dyObj,dyObj->name());
        //      return true;
        //  }
        else if (BSM_CHECK_TYPE(sc_dt::sc_logic)) {
            sc_inout<sc_dt::sc_logic > *dyObj =
                dynamic_cast<sc_inout<sc_dt::sc_logic >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        } else if (BSM_CHECK_TYPE(sc_dt::sc_bit)) {
            sc_inout<sc_dt::sc_bit > *dyObj =
                dynamic_cast<sc_inout<sc_dt::sc_bit >*>(scObj);
            assert(dyObj);
            sc_trace(tf, *dyObj, dyObj->name());
            return true;
        }
#undef BSM_CHECK_TYPE
    } else {
        if (strBSMType.compare(("sc_int")) == 0 ||
                strBSMType.compare(("sc_uint")) == 0 ||
                strBSMType.compare(("sc_bigint")) == 0 ||
                strBSMType.compare(("sc_biguint")) == 0 ||
                strBSMType.compare(("sc_fixed")) == 0 ||
                strBSMType.compare(("sc_fixed_fast")) == 0 ||
                strBSMType.compare(("sc_ufixed")) == 0) {
            tf->trace(interf, scObj->name(), 'r');
            return true;
        } else if (strBSMType.compare(("sc_bv")) == 0 ||
                strBSMType.compare(("sc_lv")) == 0) {
            tf->trace(interf, scObj->name(), 'b');
            return true;
        }
    }
    return false;
}

bool bsm_trace_object(bsm_trace_file *tf, sc_object* scObj) {
    std::string strKind = scObj->kind();
    if (strKind.compare("sc_signal") == 0 || strKind.compare("sc_clock") == 0) {
        return bsm_trace_signal(tf, scObj);
    } else if (strKind.compare("sc_in") == 0) {
        return bsm_trace_in(tf, scObj);
    } else if (strKind.compare("sc_out") == 0 ||
            strKind.compare("sc_inout") == 0) {
        return bsm_trace_out(tf, scObj);
    }
    return false;
}
/////////////////////////////////////////////////////////

}  // namespace sc_core

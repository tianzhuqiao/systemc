#ifndef SRC_SYSC_BSM_SC_BSM_TRACE_H_
#define SRC_SYSC_BSM_SC_BSM_TRACE_H_


#include <cstdio>
#include <string>
#include <vector>
#include "sysc/tracing/sc_trace_file_base.h"

namespace sc_core {

class sc_interface;
class sc_port_base;
class bsm_trace;  // defined in bsm_trace.cpp
template<class T> class bsm_T_trace;

// Print BSM error message
void bsm_put_error_message(const char* msg, bool just_warning);


// ----------------------------------------------------------------------------
//  CLASS : bsm_trace_file
//
//  ...
// ----------------------------------------------------------------------------

class bsm_trace_file
    : public sc_trace_file_base {
 public:
     enum bsm_type { BT_VCD, BT_SIMPLE };
     enum bsm_enum { BSM_WIRE = 0, BSM_REAL = 1, BSM_EVENT, BSM_TIME, BSM_LAST };

     // Create a bsm trace file.
     // `Name' forms the base of the name to which `.bsm' is added.
     bsm_trace_file(const char *name, unsigned int type = BT_SIMPLE);

     // Flush results and close file.
     ~bsm_trace_file();

 public:
     // These are all virtual functions in sc_trace_file and
     // they need to be defined here.

    // Trace sc_time, sc_event
    virtual void trace(const sc_time& object, const std::string& name);
    virtual void trace(const sc_event& object, const std::string& name);

    // Trace a boolean object (single bit)
     void trace(const bool& object, const std::string& name);

     // Trace a sc_bit object (single bit)
     virtual void trace(const sc_dt::sc_bit& object,
             const std::string& name);

     // Trace a sc_logic object (single bit)
     void trace(const sc_dt::sc_logic& object, const std::string& name);

     // Trace an unsigned char with the given width
     void trace(const unsigned char& object, const std::string& name,
             int width);

     // Trace an unsigned short with the given width
     void trace(const unsigned short& object, const std::string& name,
             int width);

     // Trace an unsigned int with the given width
     void trace(const unsigned int& object, const std::string& name,
             int width);

     // Trace an unsigned long with the given width
     void trace(const unsigned long& object, const std::string& name,
             int width);

     // Trace a signed char with the given width
     void trace(const char& object, const std::string& name, int width);

     // Trace a signed short with the given width
     void trace(const short& object, const std::string& name, int width);

     // Trace a signed int with the given width
     void trace(const int& object, const std::string& name, int width);

     // Trace a signed long with the given width
     void trace(const long& object, const std::string& name, int width);

     // Trace an int64 with a given width
     void trace(const sc_dt::int64& object, const std::string& name,
             int width);

     // Trace a uint64 with a given width
     void trace(const sc_dt::uint64& object, const std::string& name,
             int width);

     // Trace a float
     void trace(const float& object, const std::string& name);

     // Trace a double
     void trace(const double& object, const std::string& name);

     // Trace sc_dt::sc_uint_base
     void trace(const sc_dt::sc_uint_base& object,
             const std::string& name);

     // Trace sc_dt::sc_int_base
     void trace(const sc_dt::sc_int_base& object,
             const std::string& name);

     // Trace sc_dt::sc_unsigned
     void trace(const sc_dt::sc_unsigned& object,
             const std::string& name);

     // Trace sc_dt::sc_signed
     void trace(const sc_dt::sc_signed& object, const std::string& name);

     // Trace sc_dt::sc_fxval
     void trace(const sc_dt::sc_fxval& object, const std::string& name);

     // Trace sc_dt::sc_fxval_fast
     void trace(const sc_dt::sc_fxval_fast& object,
             const std::string& name);

     // Trace sc_dt::sc_fxnum
     void trace(const sc_dt::sc_fxnum& object, const std::string& name);

     // Trace sc_dt::sc_fxnum_fast
     void trace(const sc_dt::sc_fxnum_fast& object,
             const std::string& name);

     template<class T>
         void traceT(const T& object, const std::string& name,
                 bsm_enum type = BSM_WIRE) {
             if (add_trace_check(name))
                 traces.push_back(new bsm_T_trace<T>(object,
                                                     name,
                                                     obtain_name(),
                                                     type,
                                                     bsm_print_type));
         }

     // Trace sc_dt::sc_bv_base (sc_dt::sc_bv)
     virtual void trace(const sc_dt::sc_bv_base& object,
             const std::string& name);

     // Trace sc_dt::sc_lv_base (sc_dt::sc_lv)
     virtual void trace(const sc_dt::sc_lv_base& object,
             const std::string& name);
     // Trace an enumerated object - where possible output the enumeration literals
     // in the trace file. Enum literals is a null terminated array of null
     // terminated char* literal strings.
     void trace(const unsigned& object, const std::string& name,
             const char** enum_literals);

     void trace(const sc_interface* object, const std::string& name, const char data_type = 'b');
     void trace(const sc_port_base* object, const std::string& name, const char data_type = 'b');
     // Output a comment to the trace file
     void write_comment(const std::string& comment);

     // Write trace info for cycle.
     void cycle(bool delta_cycle);

 private:
    template<typename T> const T& extract_ref(const T& object) const
      { return object; }
    const sc_dt::uint64& extract_ref(const sc_event& object) const
      { return event_trigger_stamp(object); }

#if SC_TRACING_PHASE_CALLBACKS_
    // avoid hidden overload warnings
    virtual void trace(sc_trace_file*) const;
#endif  // SC_TRACING_PHASE_CALLBACKS_

    // Initialize the VCD tracing
    virtual void do_initialize();
    void print_time_stamp(unit_type now_units_high, unit_type now_units_low) const;
    bool get_time_stamp(unit_type &now_units_high, unit_type &now_units_low) const;

 protected:
     bool trace_delta_cycles;     // = 1 means trace the delta cycles

     unsigned bsm_name_index;     // Number of variables traced

     // Previous time unit as 64-bit integer
     unit_type previous_time_units_low, previous_time_units_high;

 public:
     // Array to store the variables traced
     std::vector<bsm_trace*> traces;
     bool initialized;           // = 1 means initialized

     // Create BSM names for each variable
     std::string obtain_name();

 public:
     void set_bsm_trace_type(int index,
             unsigned int nTrigger,
             unsigned int nTrace);
     void set_bsm_print_type(unsigned int type);
     unsigned int bsm_print_type;
     bool    bsm_trace_enable;
     void    enable_bsm_trace(bool bEnable = true);
     bool    is_enable_bsm_trace() { return bsm_trace_enable; }
};
// ----------------------------------------------------------------------------

// Create BSM file
extern sc_trace_file *sc_create_bsm_trace_file(const char* name,
        unsigned int type = bsm_trace_file::BT_SIMPLE);
extern void sc_close_bsm_trace_file(sc_trace_file* tf);
extern bool bsm_trace_object(bsm_trace_file *tf, sc_object* scObj);
}  // namespace sc_core
#endif   // SRC_SYSC_BSM_SC_BSM_TRACE_H_

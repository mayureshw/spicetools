#ifndef _SPICEDBG_H
#define _SPICEDBG_H

#include <iostream>
#include <list>
#include <vector>
#include <bitset>
#include <ngspice/sharedspice.h>
#include "spiceif.h"

using namespace std;

class Watch : public HexUtils
{
protected:
    const string _name;
    bool logicVal(int i, double *v)
    {
        return v[i] > logicthresh;
    }
    // Note: ngGet_Vec_Info pointers are not guaranteed to retain values
    pvector_info getvec(string name)
    {
        auto v = ngGet_Vec_Info((char*)name.c_str());
        if ( v == NULL )
        {
            cout << "Could not get vector named: " << name << endl;
            exit(1);
        }
        return v;
    }
public:
    virtual void report() = 0;
    virtual bool nextState(int) = 0;
    Watch(string name) : _name(name) {}
};

template<int sz> class VectorWatch : public Watch
{
    bitset<sz> _state;
    vector<double*> _vecs;
public:
    void report() { cout << _name << "=" << bitset2hexstr<sz>(_state) << " "; }
    bool nextState(int i)
    {
        bool changed = false;
        for(int vi = 0; vi < sz; vi++)
        {
            auto newval = logicVal(i,_vecs[vi]);
            if ( i == 0 or newval != _state[vi] )
                changed = true;
            _state[vi] = newval;
        }
        return changed;
    }
    VectorWatch(string name, list<string>& netnames) : Watch(name)
    {
        for(auto n:netnames) _vecs.push_back( getvec(n)->v_realdata );
    }
};

class TimeWatch : public Watch
{
    int _curi = 0;
    int _steps;
    double *_vec;
public:
    int steps() { return _steps; }
    void report() { printf("%e ",_vec[_curi]); }
    bool nextState(int i)
    {
        _curi = i;
        return false;
    }
    TimeWatch() : Watch("time")
    {
        auto ngvec = getvec(_name);
        _vec = ngvec->v_realdata;
        _steps = ngvec->v_length;
    }
};

class SpiceDbg : public SpiceIfBase
{
    TimeWatch *_timewatch;
    list<Watch*> _watches;
    void report()
    {
        _timewatch->report();
        for( auto w:_watches ) w->report();
        cout << endl;
    }
public:
    void addWatch( string name, string netname )
    {
        list netnames { netname };
        _watches.push_back( new VectorWatch<1>( name, netnames ) );
    }
    template <int sz> void addWatch( string name, string pref, int strt, string suf )
    {
        list<string> l;
        for(int i=strt; i<(strt+sz); i++)
            l.push_back( pref + to_string(i) + suf );
        addWatch<sz>(name,l);
    }
    template <int sz> void addWatch( string name, list<string> netnames )
    {
        static_assert( sz > 0 );
        if ( sz != netnames.size() )
        {
            cout << "Watch list and template size mismatch for " << name << endl;
            exit(1);
        }
        _watches.push_back( new VectorWatch<sz>( name, netnames ) );
    }
    void play()
    {
        auto steps = _timewatch->steps();
        for(int i=0; i<steps; i++)
        {
            bool changed = false;
            for(auto w:_watches)
                if ( w->nextState(i) ) changed = true;
            if ( changed )
            {
                _timewatch->nextState(i);
                report();
            }
        }
    }
    SpiceDbg()
    {
        loadraw();
        _timewatch = new TimeWatch(); // must be after loadraw
    }
    ~SpiceDbg()
    {
        delete _timewatch;
        for(auto w:_watches) delete w;
    }
};

#endif

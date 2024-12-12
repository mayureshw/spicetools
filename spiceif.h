#ifndef _SPICEIF_H
#define _SPICEIF_H

#include <iostream>
#include <map>
#include <vector>
#include <list>
#include <bitset>
#include <string>
#include <algorithm>
#include <ngspice/sharedspice.h>
#include "spiceconf.h"

using namespace std;

typedef map<string,unsigned> t_vecid;
typedef enum { IN, OUT } t_dir; // OUT is a misnomer, it just means non IN

class EventHandler
{
public:
    virtual bool handleEvent(bool)=0;
};

class SpiceIfBase
{
protected:
    virtual int fnSendChar(char *str)
    {
        cout << str << endl;
        cout.flush();
        return 0;
    }
    virtual int fnSendStat(char* status)
    {
        cout << "Status: " << status << endl;
        cout.flush();
        return 0;
    }
    virtual int fnControlledExit(int exitStat, NG_BOOL unloadLib, NG_BOOL onQuit)
    {
        cout << "Exiting: " << exitStat << endl;
        cout.flush();
        return 0;
    }
    // sz and vecs->veccount seemed to be same.
    // Each simulation step receives a vector of all values
    virtual int fnSendData(pvecvaluesall vecs)
    {
#ifdef SPICEDBG
        cout << "Received data vector of size:" << vecs->veccount
            << " vecindex:" << vecs->vecindex
            << endl;
        cout.flush();
#endif
        return 0;
    }
    // Seen called e.g. on every .tran, e.g. .tran 1n, 20n means it will be invoked twice
    virtual int fnSendInitData(pvecinfoall vecs)
    {
#ifdef SPICEDBG
        cout << "Received initdata"
            << " name:" << vecs->name   // e.g. transient analysis for .tran command
            << " title:" << vecs->title // simulation name from first line in the init file
            << " date:" << vecs->date   // real time
            << " type:" << vecs->type   // e.g. tran1, tran2 for .tran command
            << " veccount:" << vecs->veccount
            << endl;
        cout.flush();
#endif
        return 0;
    }
    virtual int fnBGThreadRunning(NG_BOOL inFg)
    {
        cout << "Simulation thread running in " << ( inFg ? "Foreground" : "Background" ) << endl;
        cout.flush();
        return 0;
    }
public:
    void sendCmd(string cmd)
    {
#ifdef SPICEDBG
        cout << "Cmd:" << cmd << endl;
        cout.flush();
#endif
        ngSpice_Command(const_cast<char*>(cmd.c_str()));
    }
    void sendCircCmd(string cmd) { sendCmd( string("circbyline ") + cmd ); }
    void writeraw() { sendCmd(string("write ")+rawopfile); }
    void loadraw() { sendCmd(string("load ")+rawopfile); }
    // Right command to use is 'source' with sendCmd, but with at the circbyline commands
    // that follow are getting ignored, which is strange, .include seems to work though
    void sourceFile(string flnm) { sendCircCmd(".include " + flnm); }
    // sendCmd doesn't work for initComment, tried
    void initComment() { sendCircCmd("* spiceif simulation"); }
    void setVdd() { sendCircCmd( string("Vdd vdd gnd DC ") + vddstr ); }
    void end() { sendCircCmd(".end"); }
    // It was observed that putting .tran directly in initfile does not work correctly
    void tran(string step, string stop)
    {
        sendCircCmd( string(".tran ") + step + " " + stop );
    }
    void initSpice()
    {
        SendChar *printfcn = [](char *str, int id, void *p)
            { return ((SpiceIfBase*)p)->fnSendChar(str); };

        SendStat *statfcn = [](char* status, int id, void* p)
            { return ((SpiceIfBase*)p)->fnSendStat(status); };

        ControlledExit *ngexit = [](int exitStat, NG_BOOL unloadLib, NG_BOOL onQuit, int id, void* p)
            { return ((SpiceIfBase*)p)->fnControlledExit(exitStat, unloadLib, onQuit); };

        SendData *sdata = [](pvecvaluesall vecs, int sz, int id, void* p)
            { return ((SpiceIfBase*)p)->fnSendData(vecs); };

        SendInitData *sinitdata = [](pvecinfoall vecs, int id, void* p)
            { return ((SpiceIfBase*)p)->fnSendInitData(vecs); };

        BGThreadRunning *bgtrun = [](NG_BOOL inFg, int id, void* p)
            { return ((SpiceIfBase*)p)->fnBGThreadRunning(inFg); };

        void *userData = this;

        ngSpice_Init( printfcn, statfcn, ngexit, sdata, sinitdata, bgtrun, userData );
    }

    SpiceIfBase()
    {
        initSpice();
    }
};

/* suggested contents for initfile to be sent to the constructor

* load the library file e.g.
.lib "<path/to/>sky130.lib.spice" tt

* load the library model e.g.
* If you are using Makefile.ahirasync, it should generate a trimmed models.spice
* based on subckts found in the circuit spice file
.include models.spice

* load your circuit
.include myckt.spice

*/

class HexUtils
{
public:
    int hexStrlen(int sz) { return ( sz + 3 ) / 4;  }
    template <int sz> string bitset2hexstr(bitset<sz>& bits)
    {
        auto hexlen = hexStrlen(sz);
        string retstr (hexlen,'0');
        char int2hex[] = "0123456789abcdef";
        for( int desti = hexlen - 1, bi = 0; desti >= 0; desti-- )
        {
            int nibval = 0;
            for( int nibi=0; nibi < 4 and bi < sz; nibi++, bi++ )
                nibval |= ( bits[bi] ? 1 : 0 ) << nibi;
            retstr[ desti ] = int2hex[ nibval ];
        }
        return retstr;
    }
};

class Net : public HexUtils
{
protected:
    const string _name;
    const t_dir _dir;
public:
    static inline SpiceIfBase *_spiceif;
    virtual void sendPortStr() { _spiceif->sendCircCmd( string("+") + _name ); }
    string name() { return _name; }
    virtual bool update(pvecvaluesall)=0;
    virtual void activate(t_vecid&)=0;
    virtual void report()=0;
    virtual void set(unsigned long val)=0;
    virtual void set(string val)=0;
    // pulse format PULSE(V1 V2 TD TR TF PW PER NP)
    void pulse(string duration)
    {
        _spiceif->sendCircCmd( string("V") + _name + " " + _name + " gnd pulse( 0 "
            + vddstr + " 0 1p 1p " + duration + " " + duration + " 1 "
            + ")");
    }
    bool isInput() { return _dir == IN; }
    virtual void setVsrc()
    {
        if ( isInput() ) _spiceif->sendCircCmd(
            string("V") + _name + " " + _name + " 0 0 external");
    }
    Net(string name, t_dir dir) : _name(name), _dir(dir) {}
};

// NOTE: We tried using ngGet_Vec_Info to get pointers to vector infor or its real value array
// However this information is not stable, despite the claim in the user manual rendering this
// API almost useless. We have to unfortunately build a map on every Init callback and do activation
class ScalarNet : public Net
{
    bool _logicval;
    bool _isset = false;
    int _vecid;
protected:
    double _realval;
    bool _isactivated = false;
public:
    void set(unsigned long val)
    {
        if ( not isInput() ) return;
        _logicval = val;
        _realval = val ? vdd : 0;
        _isset = true;
    }
    void set(string val)
    {
        unsigned long ival = val[0] == '0' ? 0 : 1;
        set(ival);
    }
    bool logicval() { return _logicval; }
    double realval() { return _realval; }
    void report()
    {
        if ( _isactivated )
            cout << _name << "=" << _logicval << endl;
    }
    bool update(pvecvaluesall vecs)
    {
        bool changed = false;
        if ( _isactivated )
        {
            if ( _vecid >= vecs->veccount )
            {
                cout << "In watch " << _name
                << " _vecid=" << _vecid
                << " veccount=" << vecs->veccount
                << " exiting..." << endl;
                exit(1);
            }
            _realval = vecs->vecsa[ _vecid ]->creal;
            bool logicval = _realval > logicthresh;
            changed = not _isset or logicval != _logicval;
            _logicval = logicval;
            _isset = true;
        }
        return changed;
    }
    void activate(t_vecid& vecid)
    {
        auto it = vecid.find(_name);
        if ( it == vecid.end() )
            cout << "Unknown watch ignored: " << _name << endl;
        else
        {
            _vecid = it->second;
            _isactivated = true;
            _isset = false;
        }
    }
    ScalarNet(string name, t_dir dir) : Net(name,dir) {}
};

template <int sz> class VectorNet : public Net
{
    vector<ScalarNet*> _nets {sz};
    bitset<sz> _bits;
    template<typename T> void _set(T val)
    {
        if ( not isInput() ) return;
        _bits = bitset<sz>(val);
        for(int i=0; i<sz; i++) _nets[i]->set(_bits[i]);
    }
    void hex2bin(const string &src, char *dest)
    {
        int di = sz - 1;
        // src length already validated by caller
        for( int si = src.length() - 1; di >= 0; si-- )
        {
            auto srcchar = src[si];
            string nib;
            switch ( srcchar )
            {
                case '0' :
                    nib = "0000";
                    break;
                case '1' :
                    nib = "0001";
                    break;
                case '2' :
                    nib = "0010";
                    break;
                case '3' :
                    nib = "0011";
                    break;
                case '4' :
                    nib = "0100";
                    break;
                case '5' :
                    nib = "0101";
                    break;
                case '6' :
                    nib = "0110";
                    break;
                case '7' :
                    nib = "0111";
                    break;
                case '8' :
                    nib = "1000";
                    break;
                case '9' :
                    nib = "1001";
                    break;
                case 'a' : case 'A':
                    nib = "1010";
                    break;
                case 'b' : case 'B':
                    nib = "1011";
                    break;
                case 'c' : case 'C':
                    nib = "1100";
                    break;
                case 'd' : case 'D':
                    nib = "1101";
                    break;
                case 'e' : case 'E':
                    nib = "1110";
                    break;
                case 'f' : case 'F':
                    nib = "1111";
                    break;
                default:
                    cout << "Invalid character in hex string " << srcchar << endl;
                    exit(1);
            }
            for( int ni = 3; ni >= 0 and di >= 0; ni-- )
            {
                dest[di] = nib[ni];
                di--;
            }
        }
    }
    bool spiceCompare( ScalarNet *a, ScalarNet *b )
    {
        auto an = a->name();
        auto bn = b->name();
        if (an.find(bn) == 0 || bn.find(an) == 0) {
            return an.size() > bn.size(); // Longer string comes first
        }
        return an < bn; // Otherwise, use lexicographical order
    }
    string hexstr() { return bitset2hexstr<sz>(_bits); }
public:
    vector<ScalarNet*>& subnets() { return _nets; }
    void set(unsigned long val) { _set(val); }
    void set(string val)
    {
        auto carr = val.c_str();
        char barr[sz+1] = "";
        switch ( *carr ) {
            case 'b' :
                if ( val.length() != sz + 1 )
                {
                    cout << "VectorNet: binary bitset of incorect size received. Expect " << ( sz + 1 )
                        << " Got " << val.length() << endl;
                    exit(1);
                }
                _set(carr+1);
                break;
            case 'x' : {
                int explength = hexStrlen(sz) + 1; // 1 character extra for leading 'x'
                if ( val.length() != explength )
                {
                    cout << "VectorNet: hex bitset of incorect size received. Expect " << explength
                        << " Got " << val.length() << endl;
                    exit(1);
                }
                hex2bin(val,barr);
                _set(barr);
                break;
                }
            default:
                cout << "VectorNet: bitset initialization string should start with b or x" << endl;
                exit(1);
        }
    }
    void sendPortStr()
    {
        // spice follows a collating sequence where a longer string comes before a shorter string
        // when the shorter matches leading part of the longer. Have to sort nets accordingly.
        vector<ScalarNet*> spicesorted {sz};
        partial_sort_copy( _nets.begin(), _nets.end(), spicesorted.begin(), spicesorted.end(),
             [this](ScalarNet *a, ScalarNet *b) { return spiceCompare(a,b); });
        for(auto n:spicesorted) n->sendPortStr();
    }
    void activate(t_vecid& vecid) { for(auto n:_nets) n->activate(vecid); }
    void setVsrc() { for(auto n:_nets) n->setVsrc(); }
    void report() { cout << _name.c_str() << "=" << hexstr() << endl; }
    bool update(pvecvaluesall vecs)
    {
        bool changed = false;
        for(auto n:_nets)
            if ( n->update(vecs) ) changed = true;
        for(int i=0; i<sz; i++) _bits[i] = _nets[i]->logicval();
        return changed;
    }
    VectorNet(string name, t_dir dir) : Net(name,dir)
    {
        for(int i=0; i<sz; i++)
            _nets[i] = new ScalarNet( name + to_string(i), dir );
    }
    ~VectorNet() { for(auto n:_nets) delete n; }
};

class TimeNet : public ScalarNet
{
public:
    void set(unsigned long val) {}
    void report()
    {
        if ( _isactivated )
            cout << _name << "=" << _realval << endl;
    }
    bool update(pvecvaluesall vecs)
    {
        ScalarNet::update(vecs);
        return false;
    }
    TimeNet() : ScalarNet("time",OUT) {}
};

class SpiceIf : public SpiceIfBase
{
    static inline int _id = 0; // Needed for ngSpice_Init_Sync
    TimeNet *_timenet;
    map<string,Net*> _nets;
    map<string,Net*> _subInpnets; // Only for external input subnets (for fnGetVSRCData)
    t_vecid _vecid;
    EventHandler *_eh = NULL;
    int fnGetVSRCData(double* retV, char* name, void* p)
    {
        ScalarNet *net = NULL;
        auto it = _subInpnets.find(&name[1]);
        if ( it != _subInpnets.end() ) net = (ScalarNet*) it->second;
        else
        {
            it = _nets.find(&name[1]);
            if ( it != _nets.end() ) net = (ScalarNet*) it->second;
        }
        if ( net == NULL )
        {
            cout << "fnGetVSRCData could not find net " << name << endl;
            exit(1);
        }
        else
        {
            *retV = net->realval();
#ifdef SPICEDBG
            cout << "fnGetVSRCData returning " << name << " = " << *retV << endl;
#endif
        }
        return 0;
    }
    void initSimu()
    {
        GetVSRCData *vsrcdat = [](double* retV, double time, char* name, int id, void* p)
            { return ((SpiceIf*)p)->fnGetVSRCData(retV, name, p); };
        GetISRCData *isrcdat = NULL;
        GetSyncData *syncdat = NULL;
        int *ident = &_id;
        void *userData = this;
        ngSpice_Init_Sync(vsrcdat, isrcdat, syncdat, ident, userData);

        addTimeWatch();
    }
    // sz and vecs->veccount seemed to be same.
    // Each simulation step receives a vector of all values
    int fnSendData(pvecvaluesall vecs)
    {
#ifdef SPICEDBG
        cout << "Received data vector of size:" << vecs->veccount
            << " vecindex:" << vecs->vecindex
            << endl;
        cout.flush();
#endif
        bool changed = false;
        for(auto n:_nets)
            if ( n.second->update(vecs) ) changed = true;
#ifndef SPICEDBG
        if ( changed )
#endif
        {
            for(auto n:_nets) n.second->report();
            cout << "===================" << endl;
        }
        // for real time based events such as reset, handleEvent has to be called
        // even if state didn't change, so we call it and pass 'changed' to it
        if ( _eh )
        {
            auto ehchanged = _eh->handleEvent(changed);
            if ( ehchanged )
            {
                for(auto n:_nets) n.second->report();
                cout << "~~~~~~~~~~~~~~~~~~~" << endl;
            }
        }
        return 0;
    }
    // Seen called e.g. on every .tran, e.g. .tran 1n, 20n means it will be invoked twice
    int fnSendInitData(pvecinfoall vecs)
    {
#ifdef SPICEDBG
        cout << "Received initdata"
            << " name:" << vecs->name   // e.g. transient analysis for .tran command
            << " title:" << vecs->title // simulation name from first line in the init file
            << " date:" << vecs->date   // real time
            << " type:" << vecs->type   // e.g. tran1, tran2 for .tran command
            << " veccount:" << vecs->veccount
            << endl;
        cout.flush();
#endif
        for(int i=0; i<vecs->veccount; i++)
        {
            auto vinfo = vecs->vecs[i];
            _vecid[vinfo->vecname] = vinfo->number;
        }
        for(auto n:_nets) n.second->activate(_vecid);
        return 0;
    }
public:
    template<int sz> Net* getNet(string name, t_dir dir)
    {
        auto it = _nets.find(name);
        if ( it == _nets.end() )
        {
            Net *net;
            if constexpr ( sz == 0 )
                net = new ScalarNet(name,dir);
            else
            {
                net = new VectorNet<sz>(name,dir);
                if ( net->isInput() )
                    for( auto sn : ((VectorNet<sz>*)net)->subnets() )
                        _subInpnets.emplace( sn->name(), sn );
            }
            _nets.emplace(name,net);
            return net;
        }
        else return it->second;
    }
    void setEventHandler(EventHandler *eh) { _eh = eh; }
    double getSimuTime() { return _timenet->realval(); }
    void addTimeWatch()
    {
        _timenet = new TimeNet();
        _nets.emplace("time",_timenet);
    }
    // Check the sequence of ports in the generated circuit and maintain it
    // GND and Vdd are first two ports bound by default which must not be passed
    void instantiate(string top,list<Net*> ports)
    {
        sendCircCmd( string("X") + top + " 0 Vdd " );
        for(auto p:ports) p->sendPortStr();
        sendCircCmd( string("+") + top );

        // Create V signals only for ports, not for any other nets
        // (hence we don't do this in the constructor)
        for(auto p:ports) p->setVsrc();
    }
    void run()
    {
        end();
        sendCmd("run");
    }
    SpiceIf(char *initfile)
    {
        initSimu();
        initComment();
        sourceFile(initfile);
        setVdd();
        Net::_spiceif = this;
    }
    ~SpiceIf()
    {
        for( auto n:_nets ) delete n.second;
    }
};

#endif

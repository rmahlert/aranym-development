/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sysdeps.h"
#include "dialog.h"
#include "dlgMidi.h"
#include "dlgAlert.h"
#include "parameters.h"
#include "hardware.h"
#include "midi.h"
#include "tools.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
enum MidiObjects {
    MIDI_BOX, MIDI_TITLE, MIDI_ENABLE, MIDI_CONNECTION,
    MIDI_INPUT_HELP, MIDI_INPUT, MIDI_INPUT_CHOOSE,
    MIDI_OUTPUT_HELP, MIDI_OUTPUT, MIDI_OUTPUT_CHOOSE,
    MIDI_CONNECTION_HELP, MIDI_CABLE_HELP, MIDI_STATUS,
    MIDI_REFRESH, MIDI_HELP, MIDI_APPLY, MIDI_CANCEL, MIDI_SAVE_HELP
};
char connection_label[71], input_label[58], output_label[58], status_label[71];
SGOBJ midi_dialog[] = {
    {SGBOX, SG_BACKGROUND, 0, 0,0,76,25, nullptr,0},
    {SGTEXT, 0,0, 32,1,12,1, "MIDI ports",0},
    {SGCHECKBOX, SG_SELECTABLE,0, 3,3,30,1, "Enable host MIDI connection",0},
    {SGTEXT, 0,0, 3,5,70,1, connection_label,0},
    {SGTEXT, 0,0, 3,7,70,1, "Input to Atari: keyboard / controller -> Atari software",0},
    {SGTEXT, 0,0, 3,9,57,1, input_label,0},
    {SGBUTTON, SG_SELECTABLE|SG_EXIT,0, 62,9,10,1, "Choose...",0},
    {SGTEXT, 0,0, 3,11,70,1, "Output from Atari: Atari software -> synth / host MIDI port",0},
    {SGTEXT, 0,0, 3,13,57,1, output_label,0},
    {SGBUTTON, SG_SELECTABLE|SG_EXIT,0, 62,13,10,1, "Choose...",0},
    {SGTEXT, 0,0, 3,15,70,1, "Connect a USB MIDI interface or use a host MIDI port.",0},
    {SGTEXT, 0,0, 3,16,70,1, "For MIDI cables, connect each device's OUT to the other's IN.",0},
    {SGTEXT, 0,0, 3,18,70,1, status_label,0},
    {SGBUTTON, SG_SELECTABLE|SG_EXIT,0, 3,20,14,1, "Refresh ports",0},
    {SGBUTTON, SG_SELECTABLE|SG_EXIT,0, 20,20,10,1, "Help",0},
    {SGBUTTON, SG_SELECTABLE|SG_EXIT|SG_DEFAULT,0, 45,20,12,1, "Apply",0},
    {SGBUTTON, SG_SELECTABLE|SG_EXIT|SG_CANCEL,0, 60,20,12,1, "Cancel",0},
    {SGTEXT, 0,0, 3,23,70,1, "After Apply, use Save in the main setup to keep these settings.",0},
    {-1,0,0,0,0,0,0,nullptr,0}
};

constexpr unsigned ports_per_page=10;
enum PortObjects {PORT_BOX,PORT_TITLE,PORT_FIRST,PORT_PREVIOUS=PORT_FIRST+ports_per_page,
                  PORT_PAGE,PORT_NEXT,PORT_CANCEL};
char port_title[64], port_labels[ports_per_page][71], port_page[21];
SGOBJ port_dialog[] = {
    {SGBOX, SG_BACKGROUND,0, 0,0,76,25,nullptr,0},
    {SGTEXT,0,0, 3,1,70,1,port_title,0},
    {SGBUTTON,SG_SELECTABLE|SG_EXIT,0, 3,3,70,1,port_labels[0],0},
    {SGBUTTON,SG_SELECTABLE|SG_EXIT,0, 3,5,70,1,port_labels[1],0},
    {SGBUTTON,SG_SELECTABLE|SG_EXIT,0, 3,7,70,1,port_labels[2],0},
    {SGBUTTON,SG_SELECTABLE|SG_EXIT,0, 3,9,70,1,port_labels[3],0},
    {SGBUTTON,SG_SELECTABLE|SG_EXIT,0, 3,11,70,1,port_labels[4],0},
    {SGBUTTON,SG_SELECTABLE|SG_EXIT,0, 3,13,70,1,port_labels[5],0},
    {SGBUTTON,SG_SELECTABLE|SG_EXIT,0, 3,15,70,1,port_labels[6],0},
    {SGBUTTON,SG_SELECTABLE|SG_EXIT,0, 3,17,70,1,port_labels[7],0},
    {SGBUTTON,SG_SELECTABLE|SG_EXIT,0, 3,19,70,1,port_labels[8],0},
    {SGBUTTON,SG_SELECTABLE|SG_EXIT,0, 3,21,70,1,port_labels[9],0},
    {SGBUTTON,SG_SELECTABLE|SG_EXIT,0, 3,23,12,1,"Previous",0},
    {SGTEXT,0,0, 19,23,20,1,port_page,0},
    {SGBUTTON,SG_SELECTABLE|SG_EXIT,0, 42,23,12,1,"Next",0},
    {SGBUTTON,SG_SELECTABLE|SG_EXIT|SG_CANCEL,0, 60,23,12,1,"Cancel",0},
    {-1,0,0,0,0,0,0,nullptr,0}
};

void label(char *target, size_t size, const std::string &value)
{
    if (value.size()<size) safe_strncpy(target,value.c_str(),size);
    else {
        const auto half=(size-4)/2;
        const auto shortened=value.substr(0,half)+"..."+value.substr(value.size()-half);
        safe_strncpy(target,shortened.c_str(),size);
    }
}

struct PortChoice {
    std::string selector, display;
    bool available;
};

class DlgMidiPort: public Dialog {
    std::vector<PortChoice> choices;
    size_t page=0;
    bool accepted=false;
    std::string selected;
    void update()
    {
        const auto pages=(choices.size()+ports_per_page-1)/ports_per_page;
        std::snprintf(port_page,sizeof(port_page),"Page %u of %u",unsigned(page+1),unsigned(pages));
        dlg[PORT_PREVIOUS].state=page ? 0 : SG_DISABLED;
        dlg[PORT_NEXT].state=page+1<pages ? 0 : SG_DISABLED;
        for (unsigned row=0;row<ports_per_page;++row) {
            const auto index=page*ports_per_page+row;
            dlg[PORT_FIRST+row].state=index<choices.size() ?
                (choices[index].available ? 0 : SG_DISABLED) : SG_HIDDEN;
            label(port_labels[row],sizeof(port_labels[row]),index<choices.size() ? choices[index].display : "");
        }
    }
public:
    DlgMidiPort(bool input,const std::vector<HostMidiDevice> &devices,const std::string &current)
        :Dialog(port_dialog)
    {
        safe_strncpy(port_title,input ? "Choose MIDI input to Atari" : "Choose MIDI output from Atari",sizeof(port_title));
        choices.push_back({"","None (disconnected)",true});
        const bool any=std::any_of(devices.begin(),devices.end(),[input](const HostMidiDevice &d){return input ? d.input : d.output;});
        if (any) choices.push_back({"default","Host default MIDI port",true});
        for (const auto &device:devices) {
            if (!(input ? device.input : device.output)) continue;
            const auto name=device.interface_name+": "+device.name;
            const auto matches=std::count_if(devices.begin(),devices.end(),[&](const HostMidiDevice &d){
                return (input ? d.input : d.output) && d.interface_name==device.interface_name && d.name==device.name;
            });
            const bool available=matches==1 && name.size()<sizeof(bx_options.midi.input);
            choices.push_back({name,name+(matches>1 ? " (ambiguous name)" : available ? "" : " (name too long)"),available});
        }
        for (size_t index=0;index<choices.size();++index)
            if (choices[index].selector==current) page=index/ports_per_page;
        update();
    }
    bool pressedOk() const {return accepted;}
    const std::string &selection() const {return selected;}
    int processDialog() override
    {
        if (return_obj==PORT_CANCEL) return GUI_CLOSE;
        if (return_obj==PORT_PREVIOUS && page) {--page;update();}
        else if (return_obj==PORT_NEXT && (page+1)*ports_per_page<choices.size()) {++page;update();}
        else if (return_obj>=PORT_FIRST && return_obj<PORT_PREVIOUS) {
            const auto index=page*ports_per_page+unsigned(return_obj-PORT_FIRST);
            if (index<choices.size() && choices[index].available) {
                accepted=true;selected=choices[index].selector;return GUI_CLOSE;
            }
        }
        return_obj=-1;
        return GUI_CONTINUE;
    }
};

class DlgMidi: public Dialog {
    bx_midi_options_t config;
    std::vector<HostMidiDevice> devices;
    std::string enumeration_error;
    DlgMidiPort *picker=nullptr;
    bool choosing_input=false;
    bool ports_changed=false;
    bool host_ports() const {return std::strcmp(config.type,"portmidi")==0;}
    void refresh()
    {
        devices=HostMidiTransport::devices(enumeration_error);
        update();
    }
    void update()
    {
        const bool supported=HostMidiTransport::supports_portmidi();
        const bool legacy=!host_ports() && config.type[0] && std::strcmp(config.type,"none")!=0;
        label(connection_label,sizeof(connection_label),host_ports() ? "Connection: host MIDI ports" :
              legacy ? "Connection: existing "+std::string(config.type)+" settings (kept until changed)" : "Connection: none selected");
        label(input_label,sizeof(input_label),host_ports() ? (std::strcmp(config.input,"default")==0 ? "Host default MIDI port" : config.input[0] ? config.input : "None (disconnected)") :
              legacy ? "Existing connection; choose a port to change it" : "None (disconnected)");
        label(output_label,sizeof(output_label),host_ports() ? (std::strcmp(config.output,"default")==0 ? "Host default MIDI port" : config.output[0] ? config.output : "None (disconnected)") :
              legacy ? "Existing connection; choose a port to change it" : "None (disconnected)");
        dlg[MIDI_INPUT_CHOOSE].state=dlg[MIDI_OUTPUT_CHOOSE].state=supported ? 0 : SG_DISABLED;
        if (!enumeration_error.empty()) label(status_label,sizeof(status_label),enumeration_error);
        else if (devices.empty()) safe_strncpy(status_label,"No host MIDI ports found. Connect an interface, then Refresh.",sizeof(status_label));
        else std::snprintf(status_label,sizeof(status_label),"%u host MIDI ports found. Choose input, output, or both.",unsigned(devices.size()));
    }
    bool confirm()
    {
        config.enabled=(dlg[MIDI_ENABLE].state&SG_SELECTED)!=0;
        if (config.enabled && ((host_ports() && !config.input[0] && !config.output[0]) ||
            !config.type[0] || std::strcmp(config.type,"none")==0)) {
            SDLGui_Open(DlgAlertOpen("Choose a MIDI input or output before enabling the connection.",ALERT_OK));
            return false;
        }
        const auto old=bx_options.midi;
        const bool changed=ports_changed || config.enabled!=old.enabled;
        if (!changed) return true;
        bx_options.midi=config;
        if (getMIDI()) {
            getMIDI()->enable(config.enabled);
            if (config.enabled && !bx_options.midi.enabled) {
                const auto error=getMIDI()->host_error();
                bx_options.midi=old;
                getMIDI()->enable(old.enabled);
                const std::string restore_error=old.enabled && !bx_options.midi.enabled ?
                    " The previous connection could not be restored; MIDI is disabled." : "";
                SDLGui_Open(DlgAlertOpen(("Could not connect MIDI ports. "+error+restore_error).c_str(),ALERT_OK));
                return false;
            }
        }
        return true;
    }
public:
    DlgMidi():Dialog(midi_dialog),config(bx_options.midi)
    {
        dlg[MIDI_ENABLE].state=config.enabled ? SG_SELECTED : 0;
        refresh();
    }
    int processDialog() override
    {
        switch (return_obj) {
        case MIDI_CANCEL:return GUI_CLOSE;
        case MIDI_APPLY:
            if (confirm()) return GUI_CLOSE;
            break;
        case MIDI_REFRESH:refresh();break;
        case MIDI_HELP:
            SDLGui_Open(DlgAlertOpen("Connect your USB MIDI interface before choosing its ports.\n"
                "Input sends a keyboard or controller to Atari MIDI software. Output sends Atari notes to a synth or host MIDI port.\n"
                "With MIDI cables, connect keyboard OUT to interface IN and interface OUT to synth IN.\n"
                "Choose None for a direction you do not use. MIDI is disabled until you enable it. Apply, then Save in the main setup.\n"
                "Existing file, raw-device and sequencer settings are kept unless you choose a host port.",ALERT_OK));
            break;
        case MIDI_INPUT_CHOOSE:case MIDI_OUTPUT_CHOOSE:
            choosing_input=return_obj==MIDI_INPUT_CHOOSE;
            picker=new DlgMidiPort(choosing_input,devices,host_ports() ? (choosing_input ? config.input : config.output) : "");
            SDLGui_Open(picker);
            break;
        }
        return_obj=-1;
        return GUI_CONTINUE;
    }
    void processResult() override
    {
        if (picker && picker->pressedOk()) {
            if (!host_ports()) {
                safe_strncpy(config.type,"portmidi",sizeof(config.type));
                config.input[0]=config.output[0]=0;
            }
            if (choosing_input) safe_strncpy(config.input,picker->selection().c_str(),sizeof(config.input));
            else safe_strncpy(config.output,picker->selection().c_str(),sizeof(config.output));
            ports_changed=true;
            update();
        }
        picker=nullptr;
    }
};
}

Dialog *DlgMidiOpen(void)
{
    return new DlgMidi;
}

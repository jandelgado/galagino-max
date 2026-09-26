#ifndef _MACHINES_H_
#define _MACHINES_H_

#ifdef ENABLE_PACMAN
  #include "machines/pacman/pacman.h"
#endif

#ifdef ENABLE_GALAGA
  #include "machines/galaga/galaga.h"
#endif

#ifdef ENABLE_DKONG
  #include "machines/dkong/dkong.h"
#endif

#ifdef ENABLE_FROGGER
  #include "machines/frogger/frogger.h"
#endif

#ifdef ENABLE_DIGDUG
  #include "machines/digdug/digdug.h"
#endif

#ifdef ENABLE_1942
  #include "machines/1942/1942.h"
#endif

#ifdef ENABLE_EYES
  #include "machines/eyes/eyes.h"
#endif

#ifdef ENABLE_MRTNT
  #include "machines/mrtnt/mrtnt.h"
#endif

#ifdef ENABLE_LIZWIZ
  #include "machines/lizwiz/lizwiz.h"
#endif

#ifdef ENABLE_THEGLOB
  #include "machines/theglob/theglob.h"
#endif

#ifdef ENABLE_CRUSH
  #include "machines/crush/crush.h"
#endif

#ifdef ENABLE_ANTEATER
  #include "machines/anteater/anteater.h"
#endif

#ifdef ENABLE_BOMBJACK
  #include "machines/bombjack/bombjack.h"
#endif

#ifdef ENABLE_MRDO
  #include "machines/mrdo/mrdo.h"
#endif

#ifdef ENABLE_BAGMAN
  #include "machines/bagman/bagman.h"
#endif

#ifdef ENABLE_PENGO
  #include "machines/pengo/pengo.h"
#endif

#ifdef ENABLE_MSPACMAN
  #include "machines/mspacman/mspacman.h"
#endif

#ifdef ENABLE_GALAXIAN
  #include "machines/galaxian/galaxian.h"
#endif

#ifdef ENABLE_LADYBUG
  #include "machines/ladybug/ladybug.h"
#endif

#ifdef ENABLE_SPACEINVADERS
  #include "machines/spaceinvaders/spaceinvaders.h"
#endif

#ifdef ENABLE_TIMEPLT
  #include "machines/timeplt/timeplt.h"
#endif

#ifdef ENABLE_GYRUSS
  #include "machines/gyruss/gyruss.h"
#endif

#ifdef ENABLE_TUTANKHM
  #include "machines/tutankhm/tutankhm.h"
#endif

#ifdef ENABLE_DKONGJR
  #include "machines/dkongjr/dkongjr.h"
#endif

#ifdef ENABLE_STARFORCE
  #include "machines/starforce/starforce.h"
#endif

#ifdef ENABLE_MOONCRESTA
  #include "machines/mooncresta/mooncresta.h"
#endif

#ifdef ENABLE_SCRAMBLE
  #include "machines/scramble/scramble.h"
#endif

#ifdef ENABLE_SUPERCOBRA
  #include "machines/supercobra/supercobra.h"
#endif

#ifdef ENABLE_DKONG3
  #include "machines/dkong3/dkong3.h"
#endif

#ifdef ENABLE_POOYAN
  #include "machines/pooyan/pooyan.h"
#endif

#ifdef ENABLE_PHOENIX
  #include "machines/phoenix/phoenix.h"
#endif

#ifdef ENABLE_BURGERTIME
  #include "machines/burgertime/burgertime.h"
#endif

#ifdef ENABLE_XEVIOUS
  #include "machines/xevious/xevious.h"
#endif

#ifdef ENABLE_BNJ
  #include "machines/bnj/bnj.h"
#endif

#ifdef ENABLE_MAPPY
  #include "machines/mappy/mappy.h"
#endif

#ifdef ENABLE_GAPLUS
  #include "machines/gaplus/gaplus.h"
#endif

#ifdef ENABLE_ALIBABA
  #include "machines/alibaba/alibaba.h"
#endif

#ifdef ENABLE_AMIDAR
  #include "machines/amidar/amidar.h"
#endif

#ifdef ENABLE_TURTLES
  #include "machines/turtles/turtles.h"
#endif

#ifdef ENABLE_CIRCUSC
  #include "machines/circusc/circusc.h"
#endif

#ifdef ENABLE_ROCNROPE
  #include "machines/rocnrope/rocnrope.h"
#endif

#ifdef ENABLE_TODRUAGA
  #include "machines/todruaga/todruaga.h"
#endif

#ifdef ENABLE_VANVAN
  #include "machines/vanvan/vanvan.h"
#endif

#ifdef ENABLE_PBACTION
  #include "machines/pbaction/pbaction.h"
#endif

#ifdef ENABLE_MOTORACE
  #include "machines/motorace/motorace.h"
#endif

#ifdef ENABLE_ROADFIGHTER
  #include "machines/roadfighter/roadfighter.h"
#endif

#ifdef ENABLE_FANTASY
  #include "machines/fantasy/fantasy.h"
#endif

#ifdef ENABLE_NIBBLER
  #include "machines/nibbler/nibbler.h"
#endif

#ifdef ENABLE_SCREGG
  #include "machines/scregg/scregg.h"
#endif

#ifdef ENABLE_VANGUARD
  #include "machines/vanguard/vanguard.h"
#endif

#ifdef ENABLE_ZAXXON
  #include "machines/zaxxon/zaxxon.h"
#endif

#ifdef ENABLE_CENTIPEDE
  #include "machines/centipede/centipede.h"
#endif
#ifdef ENABLE_MILLIPEDE
  #include "machines/millipede/millipede.h"
#endif

// change machine order is possible here...
machineInfo machines[] = {
#ifdef ENABLE_PACMAN
  { []() -> machineBase* { return new pacman(); }, pacman::logo,
#ifdef LED_PIN
    pacman::menuLeds,
#endif
    MCH_PACMAN },
#endif
#ifdef ENABLE_GALAGA
  { []() -> machineBase* { return new galaga(); }, galaga::logo,
#ifdef LED_PIN
    galaga::menuLeds,
#endif
    MCH_GALAGA },
#endif
#ifdef ENABLE_DIGDUG
  { []() -> machineBase* { return new digdug(); }, digdug::logo,
#ifdef LED_PIN
    digdug::menuLeds,
#endif
    MCH_DIGDUG },
#endif
#ifdef ENABLE_FROGGER
  { []() -> machineBase* { return new frogger(); }, frogger::logo,
#ifdef LED_PIN
    frogger::menuLeds,
#endif
    MCH_FROGGER },
#endif
#ifdef ENABLE_DKONG
  { []() -> machineBase* { return new dkong(); }, dkong::logo,
#ifdef LED_PIN
    dkong::menuLeds,
#endif
    MCH_DKONG },
#endif
#ifdef ENABLE_1942
  { []() -> machineBase* { return new _1942(); }, _1942::logo,
#ifdef LED_PIN
    _1942::menuLeds,
#endif
    MCH_1942 },
#endif
#ifdef ENABLE_LIZWIZ
  { []() -> machineBase* { return new lizwiz(); }, lizwiz::logo,
#ifdef LED_PIN
    lizwiz::menuLeds,
#endif
    MCH_LIZWIZ },
#endif
#ifdef ENABLE_EYES
  { []() -> machineBase* { return new eyes(); }, eyes::logo,
#ifdef LED_PIN
    eyes::menuLeds,
#endif
    MCH_EYES },
#endif
#ifdef ENABLE_MRTNT
  { []() -> machineBase* { return new mrtnt(); }, mrtnt::logo,
#ifdef LED_PIN
    mrtnt::menuLeds,
#endif
    MCH_MRTNT },
#endif
#ifdef ENABLE_THEGLOB
  { []() -> machineBase* { return new theglob(); }, theglob::logo,
#ifdef LED_PIN
    theglob::menuLeds,
#endif
    MCH_THEGLOB },
#endif
#ifdef ENABLE_CRUSH
  { []() -> machineBase* { return new crush(); }, crush::logo,
#ifdef LED_PIN
    crush::menuLeds,
#endif
    MCH_CRUSH },
#endif
#ifdef ENABLE_ANTEATER
  { []() -> machineBase* { return new anteater(); }, anteater::logo,
#ifdef LED_PIN
    anteater::menuLeds,
#endif
    MCH_ANTEATER },
#endif
#ifdef ENABLE_BOMBJACK
  { []() -> machineBase* { return new bombjack(); }, bombjack::logo,
#ifdef LED_PIN
    bombjack::menuLeds,
#endif
    MCH_BOMBJACK },
#endif
#ifdef ENABLE_MRDO
  { []() -> machineBase* { return new mrdo(); }, mrdo::logo,
#ifdef LED_PIN
    mrdo::menuLeds,
#endif
    MCH_MRDO },
#endif
#ifdef ENABLE_BAGMAN
  { []() -> machineBase* { return new bagman(); }, bagman::logo,
#ifdef LED_PIN
    bagman::menuLeds,
#endif
    MCH_BAGMAN },
#endif
#ifdef ENABLE_PENGO
  { []() -> machineBase* { return new pengo(); }, pengo::logo,
#ifdef LED_PIN
    pacman::menuLeds,
#endif
    MCH_PENGO },
#endif
#ifdef ENABLE_MSPACMAN
  { []() -> machineBase* { return new mspacman(); }, mspacman::logo,
#ifdef LED_PIN
    pacman::menuLeds,
#endif
    MCH_MSPACMAN },
#endif
#ifdef ENABLE_GALAXIAN
  { []() -> machineBase* { return new galaxian(); }, galaxian::logo,
#ifdef LED_PIN
    galaxian::menuLeds,
#endif
    MCH_GALAXIAN },
#endif
#ifdef ENABLE_LADYBUG
  { []() -> machineBase* { return new ladybug(); }, ladybug::logo,
#ifdef LED_PIN
    ladybug::menuLeds,
#endif
    MCH_LADYBUG },
#endif
#ifdef ENABLE_SPACEINVADERS
  { []() -> machineBase* { return new spaceinvaders(); }, spaceinvaders::logo,
#ifdef LED_PIN
    spaceinvaders::menuLeds,
#endif
    MCH_SPACEINVADERS },
#endif
#ifdef ENABLE_TIMEPLT
  { []() -> machineBase* { return new timeplt(); }, timeplt::logo,
#ifdef LED_PIN
    timeplt::menuLeds,
#endif
    MCH_TIMEPLT },
#endif
#ifdef ENABLE_GYRUSS
  { []() -> machineBase* { return new gyruss(); }, gyruss::logo,
#ifdef LED_PIN
    gyruss::menuLeds,
#endif
    MCH_GYRUSS },
#endif
#ifdef ENABLE_TUTANKHM
  { []() -> machineBase* { return new tutankhm(); }, tutankhm::logo,
#ifdef LED_PIN
    tutankhm::menuLeds,
#endif
    MCH_TUTANKHM },
#endif
#ifdef ENABLE_DKONGJR
  { []() -> machineBase* { return new dkongjr(); }, dkongjr::logo,
#ifdef LED_PIN
    dkong::menuLeds,
#endif
    MCH_DKONGJR },
#endif
#ifdef ENABLE_STARFORCE
  { []() -> machineBase* { return new starforce(); }, starforce::logo,
#ifdef LED_PIN
    starforce::menuLeds,
#endif
    MCH_STARFORCE },
#endif
#ifdef ENABLE_MOONCRESTA
  { []() -> machineBase* { return new mooncresta(); }, mooncresta::logo,
#ifdef LED_PIN
    mooncresta::menuLeds,
#endif
    MCH_MOONCRESTA },
#endif
#ifdef ENABLE_SCRAMBLE
  { []() -> machineBase* { return new scramble(); }, scramble::logo,
#ifdef LED_PIN
    scramble::menuLeds,
#endif
    MCH_SCRAMBLE },
#endif
#ifdef ENABLE_SUPERCOBRA
  { []() -> machineBase* { return new supercobra(); }, supercobra::logo,
#ifdef LED_PIN
    supercobra::menuLeds,
#endif
    MCH_SUPERCOBRA },
#endif
#ifdef ENABLE_DKONG3
  { []() -> machineBase* { return new dkong3(); }, dkong3::logo,
#ifdef LED_PIN
    dkong3::menuLeds,
#endif
    MCH_DKONG3 },
#endif
#ifdef ENABLE_POOYAN
  { []() -> machineBase* { return new pooyan(); }, pooyan::logo,
#ifdef LED_PIN
    pooyan::menuLeds,
#endif
    MCH_POOYAN },
#endif
#ifdef ENABLE_PHOENIX
  { []() -> machineBase* { return new phoenix(); }, phoenix::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_PHOENIX },
#endif
#ifdef ENABLE_BURGERTIME
  { []() -> machineBase* { return new burgertime(); }, burgertime::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_BURGERTIME },
#endif
#ifdef ENABLE_XEVIOUS
  { []() -> machineBase* { return new xevious(); }, xevious::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_XEVIOUS },
#endif
#ifdef ENABLE_BNJ
  { []() -> machineBase* { return new bnj(); }, bnj::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_BNJ },
#endif
#ifdef ENABLE_MAPPY
  { []() -> machineBase* { return new mappy(); }, mappy::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_MAPPY },
#endif
#ifdef ENABLE_GAPLUS
  { []() -> machineBase* { return new gaplus(); }, gaplus::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_GAPLUS },
#endif
#ifdef ENABLE_ALIBABA
  { []() -> machineBase* { return new alibaba(); }, alibaba::logo,
#ifdef LED_PIN
    pacman::menuLeds,
#endif
    MCH_ALIBABA },
#endif
#ifdef ENABLE_AMIDAR
  { []() -> machineBase* { return new amidar(); }, amidar::logo,
#ifdef LED_PIN
    amidar::menuLeds,
#endif
    MCH_AMIDAR },
#endif
#ifdef ENABLE_TURTLES
  { []() -> machineBase* { return new turtles(); }, turtles::logo,
#ifdef LED_PIN
    turtles::menuLeds,
#endif
    MCH_TURTLES },
#endif
#ifdef ENABLE_CIRCUSC
  { []() -> machineBase* { return new circusc(); }, circusc::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_CIRCUSC },
#endif
#ifdef ENABLE_ROCNROPE
  { []() -> machineBase* { return new rocnrope(); }, rocnrope::logo,
#ifdef LED_PIN
    rocnrope::menuLeds,
#endif
    MCH_ROCNROPE },
#endif
#ifdef ENABLE_TODRUAGA
  { []() -> machineBase* { return new todruaga(); }, todruaga::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_TODRUAGA },
#endif
#ifdef ENABLE_VANVAN
  { []() -> machineBase* { return new vanvan(); }, vanvan::logo,
#ifdef LED_PIN
    vanvan::menuLeds,
#endif
    MCH_VANVAN },
#endif
#ifdef ENABLE_PBACTION
  { []() -> machineBase* { return new pbaction(); }, pbaction::logo,
#ifdef LED_PIN
    pbaction::menuLeds,
#endif
    MCH_PBACTION },
#endif
#ifdef ENABLE_MOTORACE
  { []() -> machineBase* { return new motorace(); }, motorace::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_MOTORACE },
#endif
#ifdef ENABLE_ROADFIGHTER
  { []() -> machineBase* { return new roadfighter(); }, roadfighter::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_ROADFIGHTER },
#endif
#ifdef ENABLE_FANTASY
  { []() -> machineBase* { return new fantasy(); }, fantasy::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_FANTASY },
#endif
#ifdef ENABLE_NIBBLER
  { []() -> machineBase* { return new nibbler(); }, nibbler::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_NIBBLER },
#endif
#ifdef ENABLE_SCREGG
  { []() -> machineBase* { return new scregg(); }, scregg::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_SCREGG },
#endif
#ifdef ENABLE_VANGUARD
  { []() -> machineBase* { return new vanguard(); }, vanguard::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_VANGUARD },
#endif
#ifdef ENABLE_ZAXXON
  { []() -> machineBase* { return new zaxxon(); }, zaxxon::logo,
#ifdef LED_PIN
    zaxxon::menuLeds,
#endif
    MCH_ZAXXON },
#endif
#ifdef ENABLE_CENTIPEDE
  { []() -> machineBase* { return new centipede(); }, centipede::logo,
#ifdef LED_PIN
    centipede::menuLeds,
#endif
    MCH_CENTIPEDE },
#endif
#ifdef ENABLE_MILLIPEDE
  { []() -> machineBase* { return new millipede(); }, millipede::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_MILLIPEDE },
#endif
};

template <std::size_t N, class T>
constexpr std::size_t countof(T(&)[N]) { return N; }
static_assert(countof(machines) >= 1, "At least one machine has to be enabled!");

#endif // _MACHINES_H_

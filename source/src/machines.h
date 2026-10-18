#ifndef _MACHINES_H_
#define _MACHINES_H_

#include "catalog.h"

#ifdef GAME_PACMAN
  #include "machines/pacman/pacman.h"
#endif

#ifdef GAME_GALAGA
  #include "machines/galaga/galaga.h"
#endif

#ifdef GAME_DKONG
  #include "machines/dkong/dkong.h"
#endif

#ifdef GAME_FROGGER
  #include "machines/frogger/frogger.h"
#endif

#ifdef GAME_DIGDUG
  #include "machines/digdug/digdug.h"
#endif
#ifdef GAME_DIGDUG2
  #include "machines/digdug2/digdug2.h"
#endif

#ifdef GAME_1942
  #include "machines/1942/1942.h"
#endif

#ifdef GAME_EYES
  #include "machines/eyes/eyes.h"
#endif

#ifdef GAME_MRTNT
  #include "machines/mrtnt/mrtnt.h"
#endif

#ifdef GAME_LIZWIZ
  #include "machines/lizwiz/lizwiz.h"
#endif

#ifdef GAME_THEGLOB
  #include "machines/theglob/theglob.h"
#endif

#ifdef GAME_CRUSH
  #include "machines/crush/crush.h"
#endif

#ifdef GAME_ANTEATER
  #include "machines/anteater/anteater.h"
#endif

#ifdef GAME_BOMBJACK
  #include "machines/bombjack/bombjack.h"
#endif

#ifdef GAME_MRDO
  #include "machines/mrdo/mrdo.h"
#endif

#ifdef GAME_BAGMAN
  #include "machines/bagman/bagman.h"
#endif

#ifdef GAME_PENGO
  #include "machines/pengo/pengo.h"
#endif

#ifdef GAME_MSPACMAN
  #include "machines/mspacman/mspacman.h"
#endif

#ifdef GAME_GALAXIAN
  #include "machines/galaxian/galaxian.h"
#endif

#ifdef GAME_LADYBUG
  #include "machines/ladybug/ladybug.h"
#endif

#ifdef GAME_SPACEINVADERS
  #include "machines/spaceinvaders/spaceinvaders.h"
#endif

#ifdef GAME_TIMEPLT
  #include "machines/timeplt/timeplt.h"
#endif

#ifdef GAME_GYRUSS
  #include "machines/gyruss/gyruss.h"
#endif

#ifdef GAME_TUTANKHM
  #include "machines/tutankhm/tutankhm.h"
#endif

#ifdef GAME_DKONGJR
  #include "machines/dkongjr/dkongjr.h"
#endif

#ifdef GAME_STARFORCE
  #include "machines/starforce/starforce.h"
#endif

#ifdef GAME_BALUBA
  #include "machines/baluba/baluba.h"
#endif
#ifdef GAME_COMMANDO
  #include "machines/commando/commando.h"
#endif

#ifdef GAME_GNG
  #include "machines/gng/gng.h"
#endif

#ifdef GAME_TP84
  #include "machines/tp84/tp84.h"
#endif
#ifdef GAME_DEFENDER
  #include "machines/defender/defender.h"
#endif
#ifdef GAME_ROBOTRON
  #include "machines/robotron/robotron.h"
#endif
#ifdef GAME_JOUST
  #include "machines/joust/joust.h"
#endif
#ifdef GAME_SINISTAR
  #include "machines/sinistar/sinistar.h"
#endif
#ifdef GAME_YIEAR
  #include "machines/yiear/yiear.h"
#endif
#ifdef GAME_TRACKFLD
  #include "machines/trackfld/trackfld.h"
#endif
#ifdef GAME_TRON
  #include "machines/tron/tron.h"
#endif
#ifdef GAME_DOTRON
  #include "machines/dotron/dotron.h"
#endif

#ifdef GAME_MOONCRESTA
  #include "machines/mooncresta/mooncresta.h"
#endif

#ifdef GAME_SCRAMBLE
  #include "machines/scramble/scramble.h"
#endif

#ifdef GAME_SUPERCOBRA
  #include "machines/supercobra/supercobra.h"
#endif

#ifdef GAME_DKONG3
  #include "machines/dkong3/dkong3.h"
#endif

#ifdef GAME_POOYAN
  #include "machines/pooyan/pooyan.h"
#endif

#ifdef GAME_PHOENIX
  #include "machines/phoenix/phoenix.h"
#endif

#ifdef GAME_BURGERTIME
  #include "machines/burgertime/burgertime.h"
#endif

#ifdef GAME_XEVIOUS
  #include "machines/xevious/xevious.h"
#endif

#ifdef GAME_BNJ
  #include "machines/bnj/bnj.h"
#endif

#ifdef GAME_MAPPY
  #include "machines/mappy/mappy.h"
#endif

#ifdef GAME_GAPLUS
  #include "machines/gaplus/gaplus.h"
#endif

#ifdef GAME_ALIBABA
  #include "machines/alibaba/alibaba.h"
#endif

#ifdef GAME_AMIDAR
  #include "machines/amidar/amidar.h"
#endif

#ifdef GAME_TURTLES
  #include "machines/turtles/turtles.h"
#endif

#ifdef GAME_CIRCUSC
  #include "machines/circusc/circusc.h"
#endif

#ifdef GAME_ROCNROPE
  #include "machines/rocnrope/rocnrope.h"
#endif

#ifdef GAME_TODRUAGA
  #include "machines/todruaga/todruaga.h"
#endif

#ifdef GAME_PHOZON
  #include "machines/phozon/phozon.h"
#endif
#ifdef GAME_GBERET
  #include "machines/gberet/gberet.h"
#endif
#ifdef GAME_HERO
  #include "machines/hero/hero.h"
#endif
#ifdef GAME_MARBLE
  #include "machines/marble/marble.h"
#endif

#ifdef GAME_VANVAN
  #include "machines/vanvan/vanvan.h"
#endif

#ifdef GAME_PBACTION
  #include "machines/pbaction/pbaction.h"
#endif

#ifdef GAME_MOTORACE
  #include "machines/motorace/motorace.h"
#endif

#ifdef GAME_ROADFIGHTER
  #include "machines/roadfighter/roadfighter.h"
#endif

#ifdef GAME_FANTASY
  #include "machines/fantasy/fantasy.h"
#endif

#ifdef GAME_NIBBLER
  #include "machines/nibbler/nibbler.h"
#endif

#ifdef GAME_SCREGG
  #include "machines/scregg/scregg.h"
#endif

#ifdef GAME_VANGUARD
  #include "machines/vanguard/vanguard.h"
#endif

#ifdef GAME_ZAXXON
  #include "machines/zaxxon/zaxxon.h"
#endif

#ifdef GAME_CENTIPEDE
  #include "machines/centipede/centipede.h"
#endif
#ifdef GAME_MILLIPEDE
  #include "machines/millipede/millipede.h"
#endif
#ifdef GAME_ASTEROIDS
  #include "machines/asteroids/asteroids.h"
#endif
#ifdef GAME_LLANDER
  #include "machines/llander/llander.h"
#endif

#ifdef GAME_THEPIT
  #include "machines/thepit/thepit.h"
#endif

#ifdef GAME_BUBLBOBL
  #include "machines/bublbobl/bublbobl.h"
#endif

#ifdef GAME_ATETRIS
  #include "machines/atetris/atetris.h"
#endif

#ifdef GAME_CCASTLES
  #include "machines/ccastles/ccastles.h"
#endif
#ifdef GAME_BZONE
  #include "machines/bzone/bzone.h"
#endif
#ifdef GAME_TEMPEST
  #include "machines/tempest/tempest.h"
#endif
#ifdef GAME_SPACDUEL
  #include "machines/spacduel/spacduel.h"
#endif
#ifdef GAME_GRAVITAR
  #include "machines/gravitar/gravitar.h"
#endif
#ifdef GAME_BWIDOW
  #include "machines/bwidow/bwidow.h"
#endif
#ifdef GAME_BERZERK
  #include "machines/berzerk/berzerk.h"
#endif
#ifdef GAME_GORF
  #include "machines/gorf/gorf.h"
#endif
#ifdef GAME_SOLOMON
  #include "machines/solomon/solomon.h"
#endif
#ifdef GAME_STARWARS
  #include "machines/starwars/starwars.h"
#endif
#ifdef GAME_MISSILE
  #include "machines/missile/missile.h"
#endif
#ifdef GAME_MPATROL
  #include "machines/mpatrol/mpatrol.h"
#endif
#ifdef GAME_STARJACK
  #include "machines/starjack/starjack.h"
#endif
#ifdef GAME_REGULUS
  #include "machines/regulus/regulus.h"
#endif
#ifdef GAME_UPNDOWN
  #include "machines/upndown/upndown.h"
#endif
#ifdef GAME_RAFLESIA
  #include "machines/raflesia/raflesia.h"
#endif
#ifdef GAME_SWAT
  #include "machines/swat/swat.h"
#endif
#ifdef GAME_FLICKY
  #include "machines/flicky/flicky.h"
#endif
#ifdef GAME_PITFALL2
  #include "machines/pitfall2/pitfall2.h"
#endif
#ifdef GAME_MRVIKING
  #include "machines/mrviking/mrviking.h"
#endif
#ifdef GAME_IMSORRY
  #include "machines/imsorry/imsorry.h"
#endif
#ifdef GAME_WBOY
  #include "machines/wboy/wboy.h"
#endif
#ifdef GAME_FOURDWARRIO
  #include "machines/fourdwarrio/fourdwarrio.h"
#endif
#ifdef GAME_HVYMETAL
  #include "machines/hvymetal/hvymetal.h"
#endif
#ifdef GAME_SXEVIOUS
  #include "machines/sxevious/sxevious.h"
#endif
#ifdef GAME_PLEIADS
  #include "machines/pleiads/pleiads.h"
#endif
#ifdef GAME_GARDIA
  #include "machines/gardia/gardia.h"
#endif
#ifdef GAME_CAVENGER
  #include "machines/cavenger/cavenger.h"
#endif
#ifdef GAME_VULGUS
  #include "machines/vulgus/vulgus.h"
#endif
#ifdef GAME_MIKIE
  #include "machines/mikie/mikie.h"
#endif
#ifdef GAME_ARKANOID
  #include "machines/arkanoid/arkanoid.h"
#endif
#ifdef GAME_1943
  #include "machines/1943/1943.h"
  #ifdef ASSETS_REMOTE
    #include "machines/1943/1943_bundle.h"
  #endif
#endif
#ifdef GAME_CBDASH
  #include "machines/cbdash/cbdash.h"
#endif

// change machine order is possible here...
// flash, not DRAM: the table grows with every enabled game and DRAM heap
// margin for memory[] (RAMSIZE) is a few bytes. const alone lands in
// .data.rel.ro, which ESP-IDF maps to DRAM. constexpr makes any runtime
// initializer a compile error: one would write into flash at boot.
__attribute__((section(".rodata.machines")))
constexpr machineInfo machines[] = {
#ifdef GAME_PACMAN
  { createMachine<pacman>, pacman::logo,
#ifdef LED_PIN
    pacman::menuLeds,
#endif
    MCH_PACMAN },
#endif
#ifdef GAME_GALAGA
  { createMachine<galaga>, galaga::logo,
#ifdef LED_PIN
    galaga::menuLeds,
#endif
    MCH_GALAGA },
#endif
#ifdef GAME_DIGDUG
  { createMachine<digdug>, digdug::logo,
#ifdef LED_PIN
    digdug::menuLeds,
#endif
    MCH_DIGDUG },
#endif
#ifdef GAME_DIGDUG2
  { createMachine<digdug2>, digdug2::logo,
#ifdef LED_PIN
    digdug2::menuLeds,
#endif
    MCH_DIGDUG2 },
#endif
#ifdef GAME_FROGGER
  { createMachine<frogger>, frogger::logo,
#ifdef LED_PIN
    frogger::menuLeds,
#endif
    MCH_FROGGER },
#endif
#ifdef GAME_DKONG
  { createMachine<dkong>, dkong::logo,
#ifdef LED_PIN
    dkong::menuLeds,
#endif
    MCH_DKONG },
#endif
#ifdef GAME_1942
  { createMachine<_1942>, _1942::logo,
#ifdef LED_PIN
    _1942::menuLeds,
#endif
    MCH_1942, &_1942_arena_plan },
#endif
#ifdef GAME_LIZWIZ
  { createMachine<lizwiz>, lizwiz::logo,
#ifdef LED_PIN
    lizwiz::menuLeds,
#endif
    MCH_LIZWIZ },
#endif
#ifdef GAME_EYES
  { createMachine<eyes>, eyes::logo,
#ifdef LED_PIN
    eyes::menuLeds,
#endif
    MCH_EYES },
#endif
#ifdef GAME_MRTNT
  { createMachine<mrtnt>, mrtnt::logo,
#ifdef LED_PIN
    mrtnt::menuLeds,
#endif
    MCH_MRTNT },
#endif
#ifdef GAME_THEGLOB
  { createMachine<theglob>, theglob::logo,
#ifdef LED_PIN
    theglob::menuLeds,
#endif
    MCH_THEGLOB },
#endif
#ifdef GAME_CRUSH
  { createMachine<crush>, crush::logo,
#ifdef LED_PIN
    crush::menuLeds,
#endif
    MCH_CRUSH },
#endif
#ifdef GAME_ANTEATER
  { createMachine<anteater>, anteater::logo,
#ifdef LED_PIN
    anteater::menuLeds,
#endif
    MCH_ANTEATER },
#endif
#ifdef GAME_BOMBJACK
  { createMachine<bombjack>, bombjack::logo,
#ifdef LED_PIN
    bombjack::menuLeds,
#endif
    MCH_BOMBJACK, &bombjack_arena_plan },
#endif
#ifdef GAME_MRDO
  { createMachine<mrdo>, mrdo::logo,
#ifdef LED_PIN
    mrdo::menuLeds,
#endif
    MCH_MRDO },
#endif
#ifdef GAME_BAGMAN
  { createMachine<bagman>, bagman::logo,
#ifdef LED_PIN
    bagman::menuLeds,
#endif
    MCH_BAGMAN },
#endif
#ifdef GAME_PENGO
  { createMachine<pengo>, pengo::logo,
#ifdef LED_PIN
    pacman::menuLeds,
#endif
    MCH_PENGO },
#endif
#ifdef GAME_MSPACMAN
  { createMachine<mspacman>, mspacman::logo,
#ifdef LED_PIN
    pacman::menuLeds,
#endif
    MCH_MSPACMAN },
#endif
#ifdef GAME_GALAXIAN
  { createMachine<galaxian>, galaxian::logo,
#ifdef LED_PIN
    galaxian::menuLeds,
#endif
    MCH_GALAXIAN },
#endif
#ifdef GAME_LADYBUG
  { createMachine<ladybug>, ladybug::logo,
#ifdef LED_PIN
    ladybug::menuLeds,
#endif
    MCH_LADYBUG },
#endif
#ifdef GAME_SPACEINVADERS
  { createMachine<spaceinvaders>, spaceinvaders::logo,
#ifdef LED_PIN
    spaceinvaders::menuLeds,
#endif
    MCH_SPACEINVADERS },
#endif
#ifdef GAME_TIMEPLT
  { createMachine<timeplt>, timeplt::logo,
#ifdef LED_PIN
    timeplt::menuLeds,
#endif
    MCH_TIMEPLT },
#endif
#ifdef GAME_GYRUSS
  { createMachine<gyruss>, gyruss::logo,
#ifdef LED_PIN
    gyruss::menuLeds,
#endif
    MCH_GYRUSS },
#endif
#ifdef GAME_TUTANKHM
  { createMachine<tutankhm>, tutankhm::logo,
#ifdef LED_PIN
    tutankhm::menuLeds,
#endif
    MCH_TUTANKHM },
#endif
#ifdef GAME_DKONGJR
  { createMachine<dkongjr>, dkongjr::logo,
#ifdef LED_PIN
    dkong::menuLeds,
#endif
    MCH_DKONGJR },
#endif
#ifdef GAME_STARFORCE
  { createMachine<starforce>, starforce::logo,
#ifdef LED_PIN
    starforce::menuLeds,
#endif
    MCH_STARFORCE, &starforce_arena_plan },
#endif
#ifdef GAME_BALUBA
  { createMachine<baluba>, baluba::logo,
#ifdef LED_PIN
    baluba::menuLeds,
#endif
    MCH_BALUBA, &baluba_arena_plan },
#endif
#ifdef GAME_MOONCRESTA
  { createMachine<mooncresta>, mooncresta::logo,
#ifdef LED_PIN
    mooncresta::menuLeds,
#endif
    MCH_MOONCRESTA },
#endif
#ifdef GAME_SCRAMBLE
  { createMachine<scramble>, scramble::logo,
#ifdef LED_PIN
    scramble::menuLeds,
#endif
    MCH_SCRAMBLE },
#endif
#ifdef GAME_SUPERCOBRA
  { createMachine<supercobra>, supercobra::logo,
#ifdef LED_PIN
    supercobra::menuLeds,
#endif
    MCH_SUPERCOBRA },
#endif
#ifdef GAME_DKONG3
  { createMachine<dkong3>, dkong3::logo,
#ifdef LED_PIN
    dkong3::menuLeds,
#endif
    MCH_DKONG3 },
#endif
#ifdef GAME_POOYAN
  { createMachine<pooyan>, pooyan::logo,
#ifdef LED_PIN
    pooyan::menuLeds,
#endif
    MCH_POOYAN },
#endif
#ifdef GAME_PHOENIX
  { createMachine<phoenix>, phoenix::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_PHOENIX },
#endif
#ifdef GAME_BURGERTIME
  { createMachine<burgertime>, burgertime::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_BURGERTIME },
#endif
#ifdef GAME_XEVIOUS
  { createMachine<xevious>, xevious::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_XEVIOUS },
#endif
#ifdef GAME_BNJ
  { createMachine<bnj>, bnj::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_BNJ, &bnj_arena_plan },
#endif
#ifdef GAME_MAPPY
  { createMachine<mappy>, mappy::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_MAPPY },
#endif
#ifdef GAME_GAPLUS
  { createMachine<gaplus>, gaplus::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_GAPLUS },
#endif
#ifdef GAME_ALIBABA
  { createMachine<alibaba>, alibaba::logo,
#ifdef LED_PIN
    pacman::menuLeds,
#endif
    MCH_ALIBABA },
#endif
#ifdef GAME_AMIDAR
  { createMachine<amidar>, amidar::logo,
#ifdef LED_PIN
    amidar::menuLeds,
#endif
    MCH_AMIDAR },
#endif
#ifdef GAME_TURTLES
  { createMachine<turtles>, turtles::logo,
#ifdef LED_PIN
    turtles::menuLeds,
#endif
    MCH_TURTLES },
#endif
#ifdef GAME_CIRCUSC
  { createMachine<circusc>, circusc::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_CIRCUSC },
#endif
#ifdef GAME_ROCNROPE
  { createMachine<rocnrope>, rocnrope::logo,
#ifdef LED_PIN
    rocnrope::menuLeds,
#endif
    MCH_ROCNROPE },
#endif
#ifdef GAME_TODRUAGA
  { createMachine<todruaga>, todruaga::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_TODRUAGA },
#endif
#ifdef GAME_VANVAN
  { createMachine<vanvan>, vanvan::logo,
#ifdef LED_PIN
    vanvan::menuLeds,
#endif
    MCH_VANVAN },
#endif
#ifdef GAME_PBACTION
  { createMachine<pbaction>, pbaction::logo,
#ifdef LED_PIN
    pbaction::menuLeds,
#endif
    MCH_PBACTION },
#endif
#ifdef GAME_MOTORACE
  { createMachine<motorace>, motorace::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_MOTORACE },
#endif
#ifdef GAME_ROADFIGHTER
  { createMachine<roadfighter>, roadfighter::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_ROADFIGHTER },
#endif
#ifdef GAME_FANTASY
  { createMachine<fantasy>, fantasy::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_FANTASY },
#endif
#ifdef GAME_NIBBLER
  { createMachine<nibbler>, nibbler::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_NIBBLER },
#endif
#ifdef GAME_SCREGG
  { createMachine<scregg>, scregg::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_SCREGG, &scregg_arena_plan },
#endif
#ifdef GAME_VANGUARD
  { createMachine<vanguard>, vanguard::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_VANGUARD },
#endif
#ifdef GAME_ZAXXON
  { createMachine<zaxxon>, zaxxon::logo,
#ifdef LED_PIN
    zaxxon::menuLeds,
#endif
    MCH_ZAXXON, &zaxxon_arena_plan },
#endif
#ifdef GAME_CENTIPEDE
  { createMachine<centipede>, centipede::logo,
#ifdef LED_PIN
    centipede::menuLeds,
#endif
    MCH_CENTIPEDE },
#endif
#ifdef GAME_MILLIPEDE
  { createMachine<millipede>, millipede::logo,
#ifdef LED_PIN
    millipede::menuLeds,
#endif
    MCH_MILLIPEDE },
#endif
#ifdef GAME_ASTEROIDS
  { createMachine<asteroids>, asteroids::logo,
#ifdef LED_PIN
    asteroids::menuLeds,
#endif
    MCH_ASTEROIDS },
#endif
#ifdef GAME_LLANDER
  { createMachine<llander>, llander::logo,
#ifdef LED_PIN
    llander::menuLeds,
#endif
    MCH_LLANDER },
#endif
#ifdef GAME_THEPIT
  { createMachine<thepit>, thepit::logo,
#ifdef LED_PIN
    thepit::menuLeds,
#endif
    MCH_THEPIT },
#endif
#ifdef GAME_BUBLBOBL
  { createMachine<bublbobl>, bublbobl::logo,
#ifdef LED_PIN
    bublbobl::menuLeds,
#endif
    MCH_BUBLBOBL, &bublbobl_arena_plan },
#endif
#ifdef GAME_ATETRIS
  { createMachine<atetris>, atetris::logo,
#ifdef LED_PIN
    atetris::menuLeds,
#endif
    MCH_ATETRIS },
#endif
#ifdef GAME_CCASTLES
  { createMachine<ccastles>, ccastles::logo,
#ifdef LED_PIN
    ccastles::menuLeds,
#endif
    MCH_CCASTLES },
#endif
#ifdef GAME_BZONE
  { createMachine<bzone>, bzone::logo,
#ifdef LED_PIN
    bzone::menuLeds,
#endif
    MCH_BZONE },
#endif
#ifdef GAME_TEMPEST
  { createMachine<tempest>, tempest::logo,
#ifdef LED_PIN
    tempest::menuLeds,
#endif
    MCH_TEMPEST },
#endif
#ifdef GAME_SPACDUEL
  { createMachine<spacduel>, spacduel::logo,
#ifdef LED_PIN
    spacduel::menuLeds,
#endif
    MCH_SPACDUEL },
#endif
#ifdef GAME_GRAVITAR
  { createMachine<gravitar>, gravitar::logo,
#ifdef LED_PIN
    gravitar::menuLeds,
#endif
    MCH_GRAVITAR },
#endif
#ifdef GAME_BWIDOW
  { createMachine<bwidow>, bwidow::logo,
#ifdef LED_PIN
    bwidow::menuLeds,
#endif
    MCH_BWIDOW },
#endif
#ifdef GAME_BERZERK
  { createMachine<berzerk>, berzerk::logo,
#ifdef LED_PIN
    berzerk::menuLeds,
#endif
    MCH_BERZERK },
#endif
#ifdef GAME_GORF
  { createMachine<gorf>, gorf::logo,
#ifdef LED_PIN
    gorf::menuLeds,
#endif
    MCH_GORF },
#endif
#ifdef GAME_SOLOMON
  { createMachine<solomon>, solomon::logo,
#ifdef LED_PIN
    solomon::menuLeds,
#endif
    MCH_SOLOMON },
#endif
#ifdef GAME_STARWARS
  { createMachine<starwars>, starwars::logo,
#ifdef LED_PIN
    starwars::menuLeds,
#endif
    MCH_STARWARS },
#endif
#ifdef GAME_MISSILE
  { createMachine<missile>, missile::logo,
#ifdef LED_PIN
    missile::menuLeds,
#endif
    MCH_MISSILE },
#endif
#ifdef GAME_MPATROL
  { createMachine<mpatrol>, mpatrol::logo,
#ifdef LED_PIN
    mpatrol::menuLeds,
#endif
    MCH_MPATROL },
#endif
#ifdef GAME_STARJACK
  { createMachine<starjack>, starjack::logo,
#ifdef LED_PIN
    starjack::menuLeds,
#endif
    MCH_STARJACK, &starjack_arena_plan },
#endif
#ifdef GAME_REGULUS
  { createMachine<regulus>, regulus::logo,
#ifdef LED_PIN
    regulus::menuLeds,
#endif
    MCH_REGULUS, &regulus_arena_plan },
#endif
#ifdef GAME_UPNDOWN
  { createMachine<upndown>, upndown::logo,
#ifdef LED_PIN
    upndown::menuLeds,
#endif
    MCH_UPNDOWN, &upndown_arena_plan },
#endif
#ifdef GAME_RAFLESIA
  { createMachine<raflesia>, raflesia::logo,
#ifdef LED_PIN
    raflesia::menuLeds,
#endif
    MCH_RAFLESIA, &raflesia_arena_plan },
#endif
#ifdef GAME_SWAT
  { createMachine<swat>, swat::logo,
#ifdef LED_PIN
    swat::menuLeds,
#endif
    MCH_SWAT, &swat_arena_plan },
#endif
#ifdef GAME_FLICKY
  { createMachine<flicky>, flicky::logo,
#ifdef LED_PIN
    flicky::menuLeds,
#endif
    MCH_FLICKY, &flicky_arena_plan },
#endif
#ifdef GAME_PITFALL2
  { createMachine<pitfall2>, pitfall2::logo,
#ifdef LED_PIN
    pitfall2::menuLeds,
#endif
    MCH_PITFALL2, &pitfall2_arena_plan },
#endif
#ifdef GAME_MRVIKING
  { createMachine<mrviking>, mrviking::logo,
#ifdef LED_PIN
    mrviking::menuLeds,
#endif
    MCH_MRVIKING, &mrviking_arena_plan },
#endif
#ifdef GAME_IMSORRY
  { createMachine<imsorry>, imsorry::logo,
#ifdef LED_PIN
    imsorry::menuLeds,
#endif
    MCH_IMSORRY, &imsorry_arena_plan },
#endif
#ifdef GAME_WBOY
  { createMachine<wboy>, wboy::logo,
#ifdef LED_PIN
    wboy::menuLeds,
#endif
    MCH_WBOY, &wboy_arena_plan },
#endif
#ifdef GAME_FOURDWARRIO
  { createMachine<fourdwarrio>, fourdwarrio::logo,
#ifdef LED_PIN
    fourdwarrio::menuLeds,
#endif
    MCH_FOURDWARRIO, &fourdwarrio_arena_plan },
#endif
#ifdef GAME_HVYMETAL
  { createMachine<hvymetal>, hvymetal::logo,
#ifdef LED_PIN
    hvymetal::menuLeds,
#endif
    MCH_HVYMETAL, &hvymetal_arena_plan },
#endif
#ifdef GAME_SXEVIOUS
  { createMachine<sxevious>, sxevious::logo,
#ifdef LED_PIN
    sxevious::menuLeds,
#endif
    MCH_SXEVIOUS },
#endif
#ifdef GAME_PLEIADS
  { createMachine<pleiads>, pleiads::logo,
#ifdef LED_PIN
    pleiads::menuLeds,
#endif
    MCH_PLEIADS },
#endif
#ifdef GAME_GARDIA
  { createMachine<gardia>, gardia::logo,
#ifdef LED_PIN
    gardia::menuLeds,
#endif
    MCH_GARDIA, &gardia_arena_plan },
#endif
#ifdef GAME_CAVENGER
  { createMachine<cavenger>, cavenger::logo,
#ifdef LED_PIN
    cavenger::menuLeds,
#endif
    MCH_CAVENGER },
#endif
#ifdef GAME_VULGUS
  { createMachine<vulgus>, vulgus::logo,
#ifdef LED_PIN
    vulgus::menuLeds,
#endif
    MCH_VULGUS },
#endif
#ifdef GAME_MIKIE
  { createMachine<mikie>, mikie::logo,
#ifdef LED_PIN
    mikie::menuLeds,
#endif
    MCH_MIKIE },
#endif
#ifdef GAME_ARKANOID
  { createMachine<arkanoid>, arkanoid::logo,
#ifdef LED_PIN
    arkanoid::menuLeds,
#endif
    MCH_ARKANOID },
#endif
#ifdef GAME_1943
  { createMachine<_1943>, _1943::logo,
#ifdef LED_PIN
    _1943::menuLeds,
#endif
    MCH_1943,
#ifdef ASSETS_REMOTE
    nullptr, &_1943_bundle,
#endif
  },
#endif
#ifdef GAME_CBDASH
  { createMachine<cbdash>, cbdash::logo,
#ifdef LED_PIN
    cbdash::menuLeds,
#endif
    MCH_CBDASH },
#endif
#ifdef GAME_PHOZON
  { createMachine<phozon>, phozon::logo,
#ifdef LED_PIN
    phozon::menuLeds,
#endif
    MCH_PHOZON },
#endif
#ifdef GAME_GBERET
  { createMachine<gberet>, gberet::logo,
#ifdef LED_PIN
    gberet::menuLeds,
#endif
    MCH_GBERET },
#endif
#ifdef GAME_HERO
  { createMachine<hero>, hero::logo,
#ifdef LED_PIN
    hero::menuLeds,
#endif
    MCH_HERO },
#endif
#ifdef GAME_MARBLE
  { createMachine<marble>, marble::logo,
#ifdef LED_PIN
    marble::menuLeds,
#endif
    MCH_MARBLE, &marble_arena_plan },
#endif
#ifdef GAME_COMMANDO
  { createMachine<commando>, commando::logo,
#ifdef LED_PIN
    commando::menuLeds,
#endif
    MCH_COMMANDO, &commando_arena_plan },
#endif
#ifdef GAME_GNG
  { createMachine<gng>, gng::logo,
#ifdef LED_PIN
    gng::menuLeds,
#endif
    MCH_GNG, &gng_arena_plan },
#endif
#ifdef GAME_TP84
  { createMachine<tp84>, tp84::logo,
#ifdef LED_PIN
    tp84::menuLeds,
#endif
    MCH_TP84 },
#endif
#ifdef GAME_DEFENDER
  { createMachine<defender>, defender::logo,
#ifdef LED_PIN
    defender::menuLeds,
#endif
    MCH_DEFENDER, &defender_arena_plan },
#endif
#ifdef GAME_ROBOTRON
  { createMachine<robotron>, robotron::logo,
#ifdef LED_PIN
    robotron::menuLeds,
#endif
    MCH_ROBOTRON, &robotron_arena_plan },
#endif
#ifdef GAME_JOUST
  { createMachine<joust>, joust::logo,
#ifdef LED_PIN
    joust::menuLeds,
#endif
    MCH_JOUST, &joust_arena_plan },
#endif
#ifdef GAME_SINISTAR
  { createMachine<sinistar>, sinistar::logo,
#ifdef LED_PIN
    sinistar::menuLeds,
#endif
    MCH_SINISTAR, &sinistar_arena_plan },
#endif
#ifdef GAME_YIEAR
  { createMachine<yiear>, yiear::logo,
#ifdef LED_PIN
    yiear::menuLeds,
#endif
    MCH_YIEAR },
#endif
#ifdef GAME_TRACKFLD
  { createMachine<trackfld>, trackfld::logo,
#ifdef LED_PIN
    trackfld::menuLeds,
#endif
    MCH_TRACKFLD },
#endif
#ifdef GAME_TRON
  { createMachine<tron>, tron::logo,
#ifdef LED_PIN
    tron::menuLeds,
#endif
    MCH_TRON },
#endif
#ifdef GAME_DOTRON
  { createMachine<dotron>, dotron::logo,
#ifdef LED_PIN
    dotron::menuLeds,
#endif
    MCH_DOTRON },
#endif
};

template <std::size_t N, class T>
constexpr std::size_t countof(T(&)[N]) { return N; }
static_assert(countof(machines) >= 1, "At least one machine has to be enabled!");

#endif // _MACHINES_H_

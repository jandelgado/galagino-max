#ifndef _MACHINES_H_
#define _MACHINES_H_

#include "catalog.h"

#ifdef GAME_PACMAN
  #include "machines/pacman/pacman.h"
  #ifdef ASSETS_REMOTE
    #include "machines/pacman/pacman_bundle.h"
  #endif
#endif

#ifdef GAME_GALAGA
  #include "machines/galaga/galaga.h"
  #ifdef ASSETS_REMOTE
    #include "machines/galaga/galaga_bundle.h"
  #endif
#endif

#ifdef GAME_DKONG
  #include "machines/dkong/dkong.h"
  #ifdef ASSETS_REMOTE
    #include "machines/dkong/dkong_bundle.h"
  #endif
#endif

#ifdef GAME_FROGGER
  #include "machines/frogger/frogger.h"
  #ifdef ASSETS_REMOTE
    #include "machines/frogger/frogger_bundle.h"
  #endif
#endif

#ifdef GAME_DIGDUG
  #include "machines/digdug/digdug.h"
  #ifdef ASSETS_REMOTE
    #include "machines/digdug/digdug_bundle.h"
  #endif
#endif
#ifdef GAME_DIGDUG2
  #include "machines/digdug2/digdug2.h"
  #ifdef ASSETS_REMOTE
    #include "machines/digdug2/digdug2_bundle.h"
  #endif
#endif

#ifdef GAME_1942
  #include "machines/1942/1942.h"
  #ifdef ASSETS_REMOTE
    #include "machines/1942/1942_bundle.h"
  #endif
#endif

#ifdef GAME_EYES
  #include "machines/eyes/eyes.h"
  #ifdef ASSETS_REMOTE
    #include "machines/eyes/eyes_bundle.h"
  #endif
#endif

#ifdef GAME_MRTNT
  #include "machines/mrtnt/mrtnt.h"
  #ifdef ASSETS_REMOTE
    #include "machines/mrtnt/mrtnt_bundle.h"
  #endif
#endif

#ifdef GAME_LIZWIZ
  #include "machines/lizwiz/lizwiz.h"
  #ifdef ASSETS_REMOTE
    #include "machines/lizwiz/lizwiz_bundle.h"
  #endif
#endif

#ifdef GAME_THEGLOB
  #include "machines/theglob/theglob.h"
  #ifdef ASSETS_REMOTE
    #include "machines/theglob/theglob_bundle.h"
  #endif
#endif

#ifdef GAME_CRUSH
  #include "machines/crush/crush.h"
  #ifdef ASSETS_REMOTE
    #include "machines/crush/crush_bundle.h"
  #endif
#endif

#ifdef GAME_ANTEATER
  #include "machines/anteater/anteater.h"
  #ifdef ASSETS_REMOTE
    #include "machines/anteater/anteater_bundle.h"
  #endif
#endif

#ifdef GAME_BOMBJACK
  #include "machines/bombjack/bombjack.h"
  #ifdef ASSETS_REMOTE
    #include "machines/bombjack/bombjack_bundle.h"
  #endif
#endif

#ifdef GAME_MRDO
  #include "machines/mrdo/mrdo.h"
  #ifdef ASSETS_REMOTE
    #include "machines/mrdo/mrdo_bundle.h"
  #endif
#endif

#ifdef GAME_BAGMAN
  #include "machines/bagman/bagman.h"
  #ifdef ASSETS_REMOTE
    #include "machines/bagman/bagman_bundle.h"
  #endif
#endif

#ifdef GAME_PENGO
  #include "machines/pengo/pengo.h"
  #ifdef ASSETS_REMOTE
    #include "machines/pengo/pengo_bundle.h"
  #endif
#endif

#ifdef GAME_MSPACMAN
  #include "machines/mspacman/mspacman.h"
  #ifdef ASSETS_REMOTE
    #include "machines/mspacman/mspacman_bundle.h"
  #endif
#endif

#ifdef GAME_GALAXIAN
  #include "machines/galaxian/galaxian.h"
  #ifdef ASSETS_REMOTE
    #include "machines/galaxian/galaxian_bundle.h"
  #endif
#endif

#ifdef GAME_LADYBUG
  #include "machines/ladybug/ladybug.h"
  #ifdef ASSETS_REMOTE
    #include "machines/ladybug/ladybug_bundle.h"
  #endif
#endif

#ifdef GAME_SPACEINVADERS
  #include "machines/spaceinvaders/spaceinvaders.h"
  #ifdef ASSETS_REMOTE
    #include "machines/spaceinvaders/spaceinvaders_bundle.h"
  #endif
#endif

#ifdef GAME_TIMEPLT
  #include "machines/timeplt/timeplt.h"
  #ifdef ASSETS_REMOTE
    #include "machines/timeplt/timeplt_bundle.h"
  #endif
#endif

#ifdef GAME_GYRUSS
  #include "machines/gyruss/gyruss.h"
  #ifdef ASSETS_REMOTE
    #include "machines/gyruss/gyruss_bundle.h"
  #endif
#endif

#ifdef GAME_TUTANKHM
  #include "machines/tutankhm/tutankhm.h"
  #ifdef ASSETS_REMOTE
    #include "machines/tutankhm/tutankhm_bundle.h"
  #endif
#endif

#ifdef GAME_DKONGJR
  #include "machines/dkongjr/dkongjr.h"
  #ifdef ASSETS_REMOTE
    #include "machines/dkongjr/dkongjr_bundle.h"
  #endif
#endif

#ifdef GAME_STARFORCE
  #include "machines/starforce/starforce.h"
  #ifdef ASSETS_REMOTE
    #include "machines/starforce/starforce_bundle.h"
  #endif
#endif

#ifdef GAME_BALUBA
  #include "machines/baluba/baluba.h"
  #ifdef ASSETS_REMOTE
    #include "machines/baluba/baluba_bundle.h"
  #endif
#endif
#ifdef GAME_COMMANDO
  #include "machines/commando/commando.h"
  #ifdef ASSETS_REMOTE
    #include "machines/commando/commando_bundle.h"
  #endif
#endif

#ifdef GAME_GNG
  #include "machines/gng/gng.h"
  #ifdef ASSETS_REMOTE
    #include "machines/gng/gng_bundle.h"
  #endif
#endif

#ifdef GAME_TP84
  #include "machines/tp84/tp84.h"
  #ifdef ASSETS_REMOTE
    #include "machines/tp84/tp84_bundle.h"
  #endif
#endif
#ifdef GAME_DEFENDER
  #include "machines/defender/defender.h"
  #ifdef ASSETS_REMOTE
    #include "machines/defender/defender_bundle.h"
  #endif
#endif
#ifdef GAME_ROBOTRON
  #include "machines/robotron/robotron.h"
  #ifdef ASSETS_REMOTE
    #include "machines/robotron/robotron_bundle.h"
  #endif
#endif
#ifdef GAME_JOUST
  #include "machines/joust/joust.h"
  #ifdef ASSETS_REMOTE
    #include "machines/joust/joust_bundle.h"
  #endif
#endif
#ifdef GAME_SINISTAR
  #include "machines/sinistar/sinistar.h"
  #ifdef ASSETS_REMOTE
    #include "machines/sinistar/sinistar_bundle.h"
  #endif
#endif
#ifdef GAME_YIEAR
  #include "machines/yiear/yiear.h"
  #ifdef ASSETS_REMOTE
    #include "machines/yiear/yiear_bundle.h"
  #endif
#endif
#ifdef GAME_TRACKFLD
  #include "machines/trackfld/trackfld.h"
  #ifdef ASSETS_REMOTE
    #include "machines/trackfld/trackfld_bundle.h"
  #endif
#endif
#ifdef GAME_TRON
  #include "machines/tron/tron.h"
  #ifdef ASSETS_REMOTE
    #include "machines/tron/tron_bundle.h"
  #endif
#endif
#ifdef GAME_DOTRON
  #include "machines/dotron/dotron.h"
  #ifdef ASSETS_REMOTE
    #include "machines/dotron/dotron_bundle.h"
  #endif
#endif
#ifdef GAME_ELEVATOR
  #include "machines/elevator/elevator.h"
  #ifdef ASSETS_REMOTE
    #include "machines/elevator/elevator_bundle.h"
  #endif
#endif
#ifdef GAME_MHAVOC
  #include "machines/mhavoc/mhavoc.h"
  #ifdef ASSETS_REMOTE
    #include "machines/mhavoc/mhavoc_bundle.h"
  #endif
#endif
#ifdef GAME_MEGAZONE
  #include "machines/megazone/megazone.h"
  #ifdef ASSETS_REMOTE
    #include "machines/megazone/megazone_bundle.h"
  #endif
#endif
#ifdef GAME_JUNGLEH
  #include "machines/jungleh/jungleh.h"
  #ifdef ASSETS_REMOTE
    #include "machines/jungleh/jungleh_bundle.h"
  #endif
#endif
#ifdef GAME_MOTOS
  #include "machines/motos/motos.h"
  #ifdef ASSETS_REMOTE
    #include "machines/motos/motos_bundle.h"
  #endif
#endif
#ifdef GAME_SHOLLOW
  #include "machines/shollow/shollow.h"
  #ifdef ASSETS_REMOTE
    #include "machines/shollow/shollow_bundle.h"
  #endif
#endif

#ifdef GAME_MOONCRESTA
  #include "machines/mooncresta/mooncresta.h"
  #ifdef ASSETS_REMOTE
    #include "machines/mooncresta/mooncresta_bundle.h"
  #endif
#endif

#ifdef GAME_SCRAMBLE
  #include "machines/scramble/scramble.h"
  #ifdef ASSETS_REMOTE
    #include "machines/scramble/scramble_bundle.h"
  #endif
#endif

#ifdef GAME_SUPERCOBRA
  #include "machines/supercobra/supercobra.h"
  #ifdef ASSETS_REMOTE
    #include "machines/supercobra/supercobra_bundle.h"
  #endif
#endif

#ifdef GAME_DKONG3
  #include "machines/dkong3/dkong3.h"
  #ifdef ASSETS_REMOTE
    #include "machines/dkong3/dkong3_bundle.h"
  #endif
#endif

#ifdef GAME_POOYAN
  #include "machines/pooyan/pooyan.h"
  #ifdef ASSETS_REMOTE
    #include "machines/pooyan/pooyan_bundle.h"
  #endif
#endif

#ifdef GAME_PHOENIX
  #include "machines/phoenix/phoenix.h"
  #ifdef ASSETS_REMOTE
    #include "machines/phoenix/phoenix_bundle.h"
  #endif
#endif

#ifdef GAME_BURGERTIME
  #include "machines/burgertime/burgertime.h"
  #ifdef ASSETS_REMOTE
    #include "machines/burgertime/burgertime_bundle.h"
  #endif
#endif

#ifdef GAME_XEVIOUS
  #include "machines/xevious/xevious.h"
  #ifdef ASSETS_REMOTE
    #include "machines/xevious/xevious_bundle.h"
  #endif
#endif

#ifdef GAME_BNJ
  #include "machines/bnj/bnj.h"
  #ifdef ASSETS_REMOTE
    #include "machines/bnj/bnj_bundle.h"
  #endif
#endif

#ifdef GAME_MAPPY
  #include "machines/mappy/mappy.h"
  #ifdef ASSETS_REMOTE
    #include "machines/mappy/mappy_bundle.h"
  #endif
#endif

#ifdef GAME_GAPLUS
  #include "machines/gaplus/gaplus.h"
  #ifdef ASSETS_REMOTE
    #include "machines/gaplus/gaplus_bundle.h"
  #endif
#endif

#ifdef GAME_ALIBABA
  #include "machines/alibaba/alibaba.h"
  #ifdef ASSETS_REMOTE
    #include "machines/alibaba/alibaba_bundle.h"
  #endif
#endif

#ifdef GAME_AMIDAR
  #include "machines/amidar/amidar.h"
  #ifdef ASSETS_REMOTE
    #include "machines/amidar/amidar_bundle.h"
  #endif
#endif

#ifdef GAME_TURTLES
  #include "machines/turtles/turtles.h"
  #ifdef ASSETS_REMOTE
    #include "machines/turtles/turtles_bundle.h"
  #endif
#endif

#ifdef GAME_CIRCUSC
  #include "machines/circusc/circusc.h"
  #ifdef ASSETS_REMOTE
    #include "machines/circusc/circusc_bundle.h"
  #endif
#endif

#ifdef GAME_ROCNROPE
  #include "machines/rocnrope/rocnrope.h"
  #ifdef ASSETS_REMOTE
    #include "machines/rocnrope/rocnrope_bundle.h"
  #endif
#endif

#ifdef GAME_TODRUAGA
  #include "machines/todruaga/todruaga.h"
  #ifdef ASSETS_REMOTE
    #include "machines/todruaga/todruaga_bundle.h"
  #endif
#endif

#ifdef GAME_PHOZON
  #include "machines/phozon/phozon.h"
  #ifdef ASSETS_REMOTE
    #include "machines/phozon/phozon_bundle.h"
  #endif
#endif
#ifdef GAME_GBERET
  #include "machines/gberet/gberet.h"
  #ifdef ASSETS_REMOTE
    #include "machines/gberet/gberet_bundle.h"
  #endif
#endif
#ifdef GAME_HERO
  #include "machines/hero/hero.h"
  #ifdef ASSETS_REMOTE
    #include "machines/hero/hero_bundle.h"
  #endif
#endif
#ifdef GAME_MARBLE
  #include "machines/marble/marble.h"
  #ifdef ASSETS_REMOTE
    #include "machines/marble/marble_bundle.h"
  #endif
#endif

#ifdef GAME_VANVAN
  #include "machines/vanvan/vanvan.h"
  #ifdef ASSETS_REMOTE
    #include "machines/vanvan/vanvan_bundle.h"
  #endif
#endif

#ifdef GAME_PBACTION
  #include "machines/pbaction/pbaction.h"
  #ifdef ASSETS_REMOTE
    #include "machines/pbaction/pbaction_bundle.h"
  #endif
#endif

#ifdef GAME_MOTORACE
  #include "machines/motorace/motorace.h"
#endif

#ifdef GAME_ROADFIGHTER
  #include "machines/roadfighter/roadfighter.h"
  #ifdef ASSETS_REMOTE
    #include "machines/roadfighter/roadfighter_bundle.h"
  #endif
#endif

#ifdef GAME_FANTASY
  #include "machines/fantasy/fantasy.h"
  #ifdef ASSETS_REMOTE
    #include "machines/fantasy/fantasy_bundle.h"
  #endif
#endif

#ifdef GAME_NIBBLER
  #include "machines/nibbler/nibbler.h"
  #ifdef ASSETS_REMOTE
    #include "machines/nibbler/nibbler_bundle.h"
  #endif
#endif

#ifdef GAME_SCREGG
  #include "machines/scregg/scregg.h"
  #ifdef ASSETS_REMOTE
    #include "machines/scregg/scregg_bundle.h"
  #endif
#endif

#ifdef GAME_VANGUARD
  #include "machines/vanguard/vanguard.h"
  #ifdef ASSETS_REMOTE
    #include "machines/vanguard/vanguard_bundle.h"
  #endif
#endif

#ifdef GAME_ZAXXON
  #include "machines/zaxxon/zaxxon.h"
  #ifdef ASSETS_REMOTE
    #include "machines/zaxxon/zaxxon_bundle.h"
  #endif
#endif

#ifdef GAME_CENTIPEDE
  #include "machines/centipede/centipede.h"
  #ifdef ASSETS_REMOTE
    #include "machines/centipede/centipede_bundle.h"
  #endif
#endif
#ifdef GAME_MILLIPEDE
  #include "machines/millipede/millipede.h"
  #ifdef ASSETS_REMOTE
    #include "machines/millipede/millipede_bundle.h"
  #endif
#endif
#ifdef GAME_ASTEROIDS
  #include "machines/asteroids/asteroids.h"
  #ifdef ASSETS_REMOTE
    #include "machines/asteroids/asteroids_bundle.h"
  #endif
#endif
#ifdef GAME_LLANDER
  #include "machines/llander/llander.h"
  #ifdef ASSETS_REMOTE
    #include "machines/llander/llander_bundle.h"
  #endif
#endif

#ifdef GAME_THEPIT
  #include "machines/thepit/thepit.h"
  #ifdef ASSETS_REMOTE
    #include "machines/thepit/thepit_bundle.h"
  #endif
#endif

#ifdef GAME_BUBLBOBL
  #include "machines/bublbobl/bublbobl.h"
  #ifdef ASSETS_REMOTE
    #include "machines/bublbobl/bublbobl_bundle.h"
  #endif
#endif

#ifdef GAME_ATETRIS
  #include "machines/atetris/atetris.h"
  #ifdef ASSETS_REMOTE
    #include "machines/atetris/atetris_bundle.h"
  #endif
#endif

#ifdef GAME_CCASTLES
  #include "machines/ccastles/ccastles.h"
  #ifdef ASSETS_REMOTE
    #include "machines/ccastles/ccastles_bundle.h"
  #endif
#endif
#ifdef GAME_BZONE
  #include "machines/bzone/bzone.h"
  #ifdef ASSETS_REMOTE
    #include "machines/bzone/bzone_bundle.h"
  #endif
#endif
#ifdef GAME_TEMPEST
  #include "machines/tempest/tempest.h"
  #ifdef ASSETS_REMOTE
    #include "machines/tempest/tempest_bundle.h"
  #endif
#endif
#ifdef GAME_SPACDUEL
  #include "machines/spacduel/spacduel.h"
  #ifdef ASSETS_REMOTE
    #include "machines/spacduel/spacduel_bundle.h"
  #endif
#endif
#ifdef GAME_GRAVITAR
  #include "machines/gravitar/gravitar.h"
  #ifdef ASSETS_REMOTE
    #include "machines/gravitar/gravitar_bundle.h"
  #endif
#endif
#ifdef GAME_BWIDOW
  #include "machines/bwidow/bwidow.h"
  #ifdef ASSETS_REMOTE
    #include "machines/bwidow/bwidow_bundle.h"
  #endif
#endif
#ifdef GAME_BERZERK
  #include "machines/berzerk/berzerk.h"
  #ifdef ASSETS_REMOTE
    #include "machines/berzerk/berzerk_bundle.h"
  #endif
#endif
#ifdef GAME_GORF
  #include "machines/gorf/gorf.h"
  #ifdef ASSETS_REMOTE
    #include "machines/gorf/gorf_bundle.h"
  #endif
#endif
#ifdef GAME_SOLOMON
  #include "machines/solomon/solomon.h"
  #ifdef ASSETS_REMOTE
    #include "machines/solomon/solomon_bundle.h"
  #endif
#endif
#ifdef GAME_STARWARS
  #include "machines/starwars/starwars.h"
  #ifdef ASSETS_REMOTE
    #include "machines/starwars/starwars_bundle.h"
  #endif
#endif
#ifdef GAME_MISSILE
  #include "machines/missile/missile.h"
  #ifdef ASSETS_REMOTE
    #include "machines/missile/missile_bundle.h"
  #endif
#endif
#ifdef GAME_MPATROL
  #include "machines/mpatrol/mpatrol.h"
  #ifdef ASSETS_REMOTE
    #include "machines/mpatrol/mpatrol_bundle.h"
  #endif
#endif
#ifdef GAME_STARJACK
  #include "machines/starjack/starjack.h"
  #ifdef ASSETS_REMOTE
    #include "machines/starjack/starjack_bundle.h"
  #endif
#endif
#ifdef GAME_REGULUS
  #include "machines/regulus/regulus.h"
  #ifdef ASSETS_REMOTE
    #include "machines/regulus/regulus_bundle.h"
  #endif
#endif
#ifdef GAME_UPNDOWN
  #include "machines/upndown/upndown.h"
  #ifdef ASSETS_REMOTE
    #include "machines/upndown/upndown_bundle.h"
  #endif
#endif
#ifdef GAME_RAFLESIA
  #include "machines/raflesia/raflesia.h"
  #ifdef ASSETS_REMOTE
    #include "machines/raflesia/raflesia_bundle.h"
  #endif
#endif
#ifdef GAME_SWAT
  #include "machines/swat/swat.h"
  #ifdef ASSETS_REMOTE
    #include "machines/swat/swat_bundle.h"
  #endif
#endif
#ifdef GAME_FLICKY
  #include "machines/flicky/flicky.h"
  #ifdef ASSETS_REMOTE
    #include "machines/flicky/flicky_bundle.h"
  #endif
#endif
#ifdef GAME_PITFALL2
  #include "machines/pitfall2/pitfall2.h"
  #ifdef ASSETS_REMOTE
    #include "machines/pitfall2/pitfall2_bundle.h"
  #endif
#endif
#ifdef GAME_MRVIKING
  #include "machines/mrviking/mrviking.h"
  #ifdef ASSETS_REMOTE
    #include "machines/mrviking/mrviking_bundle.h"
  #endif
#endif
#ifdef GAME_IMSORRY
  #include "machines/imsorry/imsorry.h"
  #ifdef ASSETS_REMOTE
    #include "machines/imsorry/imsorry_bundle.h"
  #endif
#endif
#ifdef GAME_WBOY
  #include "machines/wboy/wboy.h"
  #ifdef ASSETS_REMOTE
    #include "machines/wboy/wboy_bundle.h"
  #endif
#endif
#ifdef GAME_FOURDWARRIO
  #include "machines/fourdwarrio/fourdwarrio.h"
  #ifdef ASSETS_REMOTE
    #include "machines/fourdwarrio/fourdwarrio_bundle.h"
  #endif
#endif
#ifdef GAME_HVYMETAL
  #include "machines/hvymetal/hvymetal.h"
  #ifdef ASSETS_REMOTE
    #include "machines/hvymetal/hvymetal_bundle.h"
  #endif
#endif
#ifdef GAME_SXEVIOUS
  #include "machines/sxevious/sxevious.h"
  #ifdef ASSETS_REMOTE
    #include "machines/sxevious/sxevious_bundle.h"
  #endif
#endif
#ifdef GAME_PLEIADS
  #include "machines/pleiads/pleiads.h"
  #ifdef ASSETS_REMOTE
    #include "machines/pleiads/pleiads_bundle.h"
  #endif
#endif
#ifdef GAME_GARDIA
  #include "machines/gardia/gardia.h"
  #ifdef ASSETS_REMOTE
    #include "machines/gardia/gardia_bundle.h"
  #endif
#endif
#ifdef GAME_CAVENGER
  #include "machines/cavenger/cavenger.h"
  #ifdef ASSETS_REMOTE
    #include "machines/cavenger/cavenger_bundle.h"
  #endif
#endif
#ifdef GAME_VULGUS
  #include "machines/vulgus/vulgus.h"
  #ifdef ASSETS_REMOTE
    #include "machines/vulgus/vulgus_bundle.h"
  #endif
#endif
#ifdef GAME_MIKIE
  #include "machines/mikie/mikie.h"
  #ifdef ASSETS_REMOTE
    #include "machines/mikie/mikie_bundle.h"
  #endif
#endif
#ifdef GAME_ARKANOID
  #include "machines/arkanoid/arkanoid.h"
  #ifdef ASSETS_REMOTE
    #include "machines/arkanoid/arkanoid_bundle.h"
  #endif
#endif
#ifdef GAME_1943
  #include "machines/1943/1943.h"
  #ifdef ASSETS_REMOTE
    #include "machines/1943/1943_bundle.h"
  #endif
#endif
#ifdef GAME_CBDASH
  #include "machines/cbdash/cbdash.h"
  #ifdef ASSETS_REMOTE
    #include "machines/cbdash/cbdash_bundle.h"
  #endif
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
    MCH_PACMAN, nullptr BUNDLE_REF(pacman_bundle_ref) },
#endif
#ifdef GAME_GALAGA
  { createMachine<galaga>, galaga::logo,
#ifdef LED_PIN
    galaga::menuLeds,
#endif
    MCH_GALAGA, nullptr BUNDLE_REF(galaga_bundle_ref) },
#endif
#ifdef GAME_DIGDUG
  { createMachine<digdug>, digdug::logo,
#ifdef LED_PIN
    digdug::menuLeds,
#endif
    MCH_DIGDUG, nullptr BUNDLE_REF(digdug_bundle_ref) },
#endif
#ifdef GAME_DIGDUG2
  { createMachine<digdug2>, digdug2::logo,
#ifdef LED_PIN
    digdug2::menuLeds,
#endif
    MCH_DIGDUG2, nullptr BUNDLE_REF(digdug2_bundle_ref) },
#endif
#ifdef GAME_FROGGER
  { createMachine<frogger>, frogger::logo,
#ifdef LED_PIN
    frogger::menuLeds,
#endif
    MCH_FROGGER, nullptr BUNDLE_REF(frogger_bundle_ref) },
#endif
#ifdef GAME_DKONG
  { createMachine<dkong>, dkong::logo,
#ifdef LED_PIN
    dkong::menuLeds,
#endif
    MCH_DKONG, nullptr BUNDLE_REF(dkong_bundle_ref) },
#endif
#ifdef GAME_1942
  { createMachine<_1942>, _1942::logo,
#ifdef LED_PIN
    _1942::menuLeds,
#endif
    MCH_1942, &_1942_arena_plan BUNDLE_REF(_1942_bundle_ref) },
#endif
#ifdef GAME_LIZWIZ
  { createMachine<lizwiz>, lizwiz::logo,
#ifdef LED_PIN
    lizwiz::menuLeds,
#endif
    MCH_LIZWIZ, nullptr BUNDLE_REF(lizwiz_bundle_ref) },
#endif
#ifdef GAME_EYES
  { createMachine<eyes>, eyes::logo,
#ifdef LED_PIN
    eyes::menuLeds,
#endif
    MCH_EYES, nullptr BUNDLE_REF(eyes_bundle_ref) },
#endif
#ifdef GAME_MRTNT
  { createMachine<mrtnt>, mrtnt::logo,
#ifdef LED_PIN
    mrtnt::menuLeds,
#endif
    MCH_MRTNT, nullptr BUNDLE_REF(mrtnt_bundle_ref) },
#endif
#ifdef GAME_THEGLOB
  { createMachine<theglob>, theglob::logo,
#ifdef LED_PIN
    theglob::menuLeds,
#endif
    MCH_THEGLOB, nullptr BUNDLE_REF(theglob_bundle_ref) },
#endif
#ifdef GAME_CRUSH
  { createMachine<crush>, crush::logo,
#ifdef LED_PIN
    crush::menuLeds,
#endif
    MCH_CRUSH, nullptr BUNDLE_REF(crush_bundle_ref) },
#endif
#ifdef GAME_ANTEATER
  { createMachine<anteater>, anteater::logo,
#ifdef LED_PIN
    anteater::menuLeds,
#endif
    MCH_ANTEATER, nullptr BUNDLE_REF(anteater_bundle_ref) },
#endif
#ifdef GAME_BOMBJACK
  { createMachine<bombjack>, bombjack::logo,
#ifdef LED_PIN
    bombjack::menuLeds,
#endif
    MCH_BOMBJACK, &bombjack_arena_plan BUNDLE_REF(bombjack_bundle_ref) },
#endif
#ifdef GAME_MRDO
  { createMachine<mrdo>, mrdo::logo,
#ifdef LED_PIN
    mrdo::menuLeds,
#endif
    MCH_MRDO, nullptr BUNDLE_REF(mrdo_bundle_ref) },
#endif
#ifdef GAME_BAGMAN
  { createMachine<bagman>, bagman::logo,
#ifdef LED_PIN
    bagman::menuLeds,
#endif
    MCH_BAGMAN, nullptr BUNDLE_REF(bagman_bundle_ref) },
#endif
#ifdef GAME_PENGO
  { createMachine<pengo>, pengo::logo,
#ifdef LED_PIN
    pacman::menuLeds,
#endif
    MCH_PENGO, nullptr BUNDLE_REF(pengo_bundle_ref) },
#endif
#ifdef GAME_MSPACMAN
  { createMachine<mspacman>, mspacman::logo,
#ifdef LED_PIN
    pacman::menuLeds,
#endif
    MCH_MSPACMAN, nullptr BUNDLE_REF(mspacman_bundle_ref) },
#endif
#ifdef GAME_GALAXIAN
  { createMachine<galaxian>, galaxian::logo,
#ifdef LED_PIN
    galaxian::menuLeds,
#endif
    MCH_GALAXIAN, nullptr BUNDLE_REF(galaxian_bundle_ref) },
#endif
#ifdef GAME_LADYBUG
  { createMachine<ladybug>, ladybug::logo,
#ifdef LED_PIN
    ladybug::menuLeds,
#endif
    MCH_LADYBUG, nullptr BUNDLE_REF(ladybug_bundle_ref) },
#endif
#ifdef GAME_SPACEINVADERS
  { createMachine<spaceinvaders>, spaceinvaders::logo,
#ifdef LED_PIN
    spaceinvaders::menuLeds,
#endif
    MCH_SPACEINVADERS, nullptr BUNDLE_REF(spaceinvaders_bundle_ref) },
#endif
#ifdef GAME_TIMEPLT
  { createMachine<timeplt>, timeplt::logo,
#ifdef LED_PIN
    timeplt::menuLeds,
#endif
    MCH_TIMEPLT, nullptr BUNDLE_REF(timeplt_bundle_ref) },
#endif
#ifdef GAME_GYRUSS
  { createMachine<gyruss>, gyruss::logo,
#ifdef LED_PIN
    gyruss::menuLeds,
#endif
    MCH_GYRUSS, nullptr BUNDLE_REF(gyruss_bundle_ref) },
#endif
#ifdef GAME_TUTANKHM
  { createMachine<tutankhm>, tutankhm::logo,
#ifdef LED_PIN
    tutankhm::menuLeds,
#endif
    MCH_TUTANKHM, nullptr BUNDLE_REF(tutankhm_bundle_ref) },
#endif
#ifdef GAME_DKONGJR
  { createMachine<dkongjr>, dkongjr::logo,
#ifdef LED_PIN
    dkong::menuLeds,
#endif
    MCH_DKONGJR, nullptr BUNDLE_REF(dkongjr_bundle_ref) },
#endif
#ifdef GAME_STARFORCE
  { createMachine<starforce>, starforce::logo,
#ifdef LED_PIN
    starforce::menuLeds,
#endif
    MCH_STARFORCE, &starforce_arena_plan BUNDLE_REF(starforce_bundle_ref) },
#endif
#ifdef GAME_BALUBA
  { createMachine<baluba>, baluba::logo,
#ifdef LED_PIN
    baluba::menuLeds,
#endif
    MCH_BALUBA, &baluba_arena_plan BUNDLE_REF(baluba_bundle_ref) },
#endif
#ifdef GAME_MOONCRESTA
  { createMachine<mooncresta>, mooncresta::logo,
#ifdef LED_PIN
    mooncresta::menuLeds,
#endif
    MCH_MOONCRESTA, nullptr BUNDLE_REF(mooncresta_bundle_ref) },
#endif
#ifdef GAME_SCRAMBLE
  { createMachine<scramble>, scramble::logo,
#ifdef LED_PIN
    scramble::menuLeds,
#endif
    MCH_SCRAMBLE, nullptr BUNDLE_REF(scramble_bundle_ref) },
#endif
#ifdef GAME_SUPERCOBRA
  { createMachine<supercobra>, supercobra::logo,
#ifdef LED_PIN
    supercobra::menuLeds,
#endif
    MCH_SUPERCOBRA, nullptr BUNDLE_REF(supercobra_bundle_ref) },
#endif
#ifdef GAME_DKONG3
  { createMachine<dkong3>, dkong3::logo,
#ifdef LED_PIN
    dkong3::menuLeds,
#endif
    MCH_DKONG3, nullptr BUNDLE_REF(dkong3_bundle_ref) },
#endif
#ifdef GAME_POOYAN
  { createMachine<pooyan>, pooyan::logo,
#ifdef LED_PIN
    pooyan::menuLeds,
#endif
    MCH_POOYAN, nullptr BUNDLE_REF(pooyan_bundle_ref) },
#endif
#ifdef GAME_PHOENIX
  { createMachine<phoenix>, phoenix::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_PHOENIX, nullptr BUNDLE_REF(phoenix_bundle_ref) },
#endif
#ifdef GAME_BURGERTIME
  { createMachine<burgertime>, burgertime::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_BURGERTIME, nullptr BUNDLE_REF(burgertime_bundle_ref) },
#endif
#ifdef GAME_XEVIOUS
  { createMachine<xevious>, xevious::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_XEVIOUS, nullptr BUNDLE_REF(xevious_bundle_ref) },
#endif
#ifdef GAME_BNJ
  { createMachine<bnj>, bnj::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_BNJ, &bnj_arena_plan BUNDLE_REF(bnj_bundle_ref) },
#endif
#ifdef GAME_MAPPY
  { createMachine<mappy>, mappy::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_MAPPY, nullptr BUNDLE_REF(mappy_bundle_ref) },
#endif
#ifdef GAME_GAPLUS
  { createMachine<gaplus>, gaplus::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_GAPLUS, nullptr BUNDLE_REF(gaplus_bundle_ref) },
#endif
#ifdef GAME_ALIBABA
  { createMachine<alibaba>, alibaba::logo,
#ifdef LED_PIN
    pacman::menuLeds,
#endif
    MCH_ALIBABA, nullptr BUNDLE_REF(alibaba_bundle_ref) },
#endif
#ifdef GAME_AMIDAR
  { createMachine<amidar>, amidar::logo,
#ifdef LED_PIN
    amidar::menuLeds,
#endif
    MCH_AMIDAR, nullptr BUNDLE_REF(amidar_bundle_ref) },
#endif
#ifdef GAME_TURTLES
  { createMachine<turtles>, turtles::logo,
#ifdef LED_PIN
    turtles::menuLeds,
#endif
    MCH_TURTLES, nullptr BUNDLE_REF(turtles_bundle_ref) },
#endif
#ifdef GAME_CIRCUSC
  { createMachine<circusc>, circusc::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_CIRCUSC, nullptr BUNDLE_REF(circusc_bundle_ref) },
#endif
#ifdef GAME_ROCNROPE
  { createMachine<rocnrope>, rocnrope::logo,
#ifdef LED_PIN
    rocnrope::menuLeds,
#endif
    MCH_ROCNROPE, nullptr BUNDLE_REF(rocnrope_bundle_ref) },
#endif
#ifdef GAME_TODRUAGA
  { createMachine<todruaga>, todruaga::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_TODRUAGA, nullptr BUNDLE_REF(todruaga_bundle_ref) },
#endif
#ifdef GAME_VANVAN
  { createMachine<vanvan>, vanvan::logo,
#ifdef LED_PIN
    vanvan::menuLeds,
#endif
    MCH_VANVAN, nullptr BUNDLE_REF(vanvan_bundle_ref) },
#endif
#ifdef GAME_PBACTION
  { createMachine<pbaction>, pbaction::logo,
#ifdef LED_PIN
    pbaction::menuLeds,
#endif
    MCH_PBACTION, nullptr BUNDLE_REF(pbaction_bundle_ref) },
#endif
#ifdef GAME_MOTORACE
  { createMachine<motorace>, motorace::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_MOTORACE, nullptr BUNDLE_REF(nullptr) },
#endif
#ifdef GAME_ROADFIGHTER
  { createMachine<roadfighter>, roadfighter::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_ROADFIGHTER, nullptr BUNDLE_REF(roadfighter_bundle_ref) },
#endif
#ifdef GAME_FANTASY
  { createMachine<fantasy>, fantasy::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_FANTASY, nullptr BUNDLE_REF(fantasy_bundle_ref) },
#endif
#ifdef GAME_NIBBLER
  { createMachine<nibbler>, nibbler::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_NIBBLER, nullptr BUNDLE_REF(nibbler_bundle_ref) },
#endif
#ifdef GAME_SCREGG
  { createMachine<scregg>, scregg::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_SCREGG, &scregg_arena_plan BUNDLE_REF(scregg_bundle_ref) },
#endif
#ifdef GAME_VANGUARD
  { createMachine<vanguard>, vanguard::logo,
#ifdef LED_PIN
    machineBase::defaultMenuLeds,
#endif
    MCH_VANGUARD, nullptr BUNDLE_REF(vanguard_bundle_ref) },
#endif
#ifdef GAME_ZAXXON
  { createMachine<zaxxon>, zaxxon::logo,
#ifdef LED_PIN
    zaxxon::menuLeds,
#endif
    MCH_ZAXXON, &zaxxon_arena_plan BUNDLE_REF(zaxxon_bundle_ref) },
#endif
#ifdef GAME_CENTIPEDE
  { createMachine<centipede>, centipede::logo,
#ifdef LED_PIN
    centipede::menuLeds,
#endif
    MCH_CENTIPEDE, nullptr BUNDLE_REF(centipede_bundle_ref) },
#endif
#ifdef GAME_MILLIPEDE
  { createMachine<millipede>, millipede::logo,
#ifdef LED_PIN
    millipede::menuLeds,
#endif
    MCH_MILLIPEDE, nullptr BUNDLE_REF(millipede_bundle_ref) },
#endif
#ifdef GAME_ASTEROIDS
  { createMachine<asteroids>, asteroids::logo,
#ifdef LED_PIN
    asteroids::menuLeds,
#endif
    MCH_ASTEROIDS, nullptr BUNDLE_REF(asteroids_bundle_ref) },
#endif
#ifdef GAME_LLANDER
  { createMachine<llander>, llander::logo,
#ifdef LED_PIN
    llander::menuLeds,
#endif
    MCH_LLANDER, nullptr BUNDLE_REF(llander_bundle_ref) },
#endif
#ifdef GAME_THEPIT
  { createMachine<thepit>, thepit::logo,
#ifdef LED_PIN
    thepit::menuLeds,
#endif
    MCH_THEPIT, nullptr BUNDLE_REF(thepit_bundle_ref) },
#endif
#ifdef GAME_BUBLBOBL
  { createMachine<bublbobl>, bublbobl::logo,
#ifdef LED_PIN
    bublbobl::menuLeds,
#endif
    MCH_BUBLBOBL, &bublbobl_arena_plan BUNDLE_REF(bublbobl_bundle_ref) },
#endif
#ifdef GAME_ATETRIS
  { createMachine<atetris>, atetris::logo,
#ifdef LED_PIN
    atetris::menuLeds,
#endif
    MCH_ATETRIS, nullptr BUNDLE_REF(atetris_bundle_ref) },
#endif
#ifdef GAME_CCASTLES
  { createMachine<ccastles>, ccastles::logo,
#ifdef LED_PIN
    ccastles::menuLeds,
#endif
    MCH_CCASTLES, nullptr BUNDLE_REF(ccastles_bundle_ref) },
#endif
#ifdef GAME_BZONE
  { createMachine<bzone>, bzone::logo,
#ifdef LED_PIN
    bzone::menuLeds,
#endif
    MCH_BZONE, nullptr BUNDLE_REF(bzone_bundle_ref) },
#endif
#ifdef GAME_TEMPEST
  { createMachine<tempest>, tempest::logo,
#ifdef LED_PIN
    tempest::menuLeds,
#endif
    MCH_TEMPEST, nullptr BUNDLE_REF(tempest_bundle_ref) },
#endif
#ifdef GAME_SPACDUEL
  { createMachine<spacduel>, spacduel::logo,
#ifdef LED_PIN
    spacduel::menuLeds,
#endif
    MCH_SPACDUEL, nullptr BUNDLE_REF(spacduel_bundle_ref) },
#endif
#ifdef GAME_GRAVITAR
  { createMachine<gravitar>, gravitar::logo,
#ifdef LED_PIN
    gravitar::menuLeds,
#endif
    MCH_GRAVITAR, nullptr BUNDLE_REF(gravitar_bundle_ref) },
#endif
#ifdef GAME_BWIDOW
  { createMachine<bwidow>, bwidow::logo,
#ifdef LED_PIN
    bwidow::menuLeds,
#endif
    MCH_BWIDOW, nullptr BUNDLE_REF(bwidow_bundle_ref) },
#endif
#ifdef GAME_BERZERK
  { createMachine<berzerk>, berzerk::logo,
#ifdef LED_PIN
    berzerk::menuLeds,
#endif
    MCH_BERZERK, nullptr BUNDLE_REF(berzerk_bundle_ref) },
#endif
#ifdef GAME_GORF
  { createMachine<gorf>, gorf::logo,
#ifdef LED_PIN
    gorf::menuLeds,
#endif
    MCH_GORF, nullptr BUNDLE_REF(gorf_bundle_ref) },
#endif
#ifdef GAME_SOLOMON
  { createMachine<solomon>, solomon::logo,
#ifdef LED_PIN
    solomon::menuLeds,
#endif
    MCH_SOLOMON, nullptr BUNDLE_REF(solomon_bundle_ref) },
#endif
#ifdef GAME_STARWARS
  { createMachine<starwars>, starwars::logo,
#ifdef LED_PIN
    starwars::menuLeds,
#endif
    MCH_STARWARS, nullptr BUNDLE_REF(starwars_bundle_ref) },
#endif
#ifdef GAME_MISSILE
  { createMachine<missile>, missile::logo,
#ifdef LED_PIN
    missile::menuLeds,
#endif
    MCH_MISSILE, nullptr BUNDLE_REF(missile_bundle_ref) },
#endif
#ifdef GAME_MPATROL
  { createMachine<mpatrol>, mpatrol::logo,
#ifdef LED_PIN
    mpatrol::menuLeds,
#endif
    MCH_MPATROL, nullptr BUNDLE_REF(mpatrol_bundle_ref) },
#endif
#ifdef GAME_STARJACK
  { createMachine<starjack>, starjack::logo,
#ifdef LED_PIN
    starjack::menuLeds,
#endif
    MCH_STARJACK, &starjack_arena_plan BUNDLE_REF(starjack_bundle_ref) },
#endif
#ifdef GAME_REGULUS
  { createMachine<regulus>, regulus::logo,
#ifdef LED_PIN
    regulus::menuLeds,
#endif
    MCH_REGULUS, &regulus_arena_plan BUNDLE_REF(regulus_bundle_ref) },
#endif
#ifdef GAME_UPNDOWN
  { createMachine<upndown>, upndown::logo,
#ifdef LED_PIN
    upndown::menuLeds,
#endif
    MCH_UPNDOWN, &upndown_arena_plan BUNDLE_REF(upndown_bundle_ref) },
#endif
#ifdef GAME_RAFLESIA
  { createMachine<raflesia>, raflesia::logo,
#ifdef LED_PIN
    raflesia::menuLeds,
#endif
    MCH_RAFLESIA, &raflesia_arena_plan BUNDLE_REF(raflesia_bundle_ref) },
#endif
#ifdef GAME_SWAT
  { createMachine<swat>, swat::logo,
#ifdef LED_PIN
    swat::menuLeds,
#endif
    MCH_SWAT, &swat_arena_plan BUNDLE_REF(swat_bundle_ref) },
#endif
#ifdef GAME_FLICKY
  { createMachine<flicky>, flicky::logo,
#ifdef LED_PIN
    flicky::menuLeds,
#endif
    MCH_FLICKY, &flicky_arena_plan BUNDLE_REF(flicky_bundle_ref) },
#endif
#ifdef GAME_PITFALL2
  { createMachine<pitfall2>, pitfall2::logo,
#ifdef LED_PIN
    pitfall2::menuLeds,
#endif
    MCH_PITFALL2, &pitfall2_arena_plan BUNDLE_REF(pitfall2_bundle_ref) },
#endif
#ifdef GAME_MRVIKING
  { createMachine<mrviking>, mrviking::logo,
#ifdef LED_PIN
    mrviking::menuLeds,
#endif
    MCH_MRVIKING, &mrviking_arena_plan BUNDLE_REF(mrviking_bundle_ref) },
#endif
#ifdef GAME_IMSORRY
  { createMachine<imsorry>, imsorry::logo,
#ifdef LED_PIN
    imsorry::menuLeds,
#endif
    MCH_IMSORRY, &imsorry_arena_plan BUNDLE_REF(imsorry_bundle_ref) },
#endif
#ifdef GAME_WBOY
  { createMachine<wboy>, wboy::logo,
#ifdef LED_PIN
    wboy::menuLeds,
#endif
    MCH_WBOY, &wboy_arena_plan BUNDLE_REF(wboy_bundle_ref) },
#endif
#ifdef GAME_FOURDWARRIO
  { createMachine<fourdwarrio>, fourdwarrio::logo,
#ifdef LED_PIN
    fourdwarrio::menuLeds,
#endif
    MCH_FOURDWARRIO, &fourdwarrio_arena_plan BUNDLE_REF(fourdwarrio_bundle_ref) },
#endif
#ifdef GAME_HVYMETAL
  { createMachine<hvymetal>, hvymetal::logo,
#ifdef LED_PIN
    hvymetal::menuLeds,
#endif
    MCH_HVYMETAL, &hvymetal_arena_plan BUNDLE_REF(hvymetal_bundle_ref) },
#endif
#ifdef GAME_SXEVIOUS
  { createMachine<sxevious>, sxevious::logo,
#ifdef LED_PIN
    sxevious::menuLeds,
#endif
    MCH_SXEVIOUS, nullptr BUNDLE_REF(sxevious_bundle_ref) },
#endif
#ifdef GAME_PLEIADS
  { createMachine<pleiads>, pleiads::logo,
#ifdef LED_PIN
    pleiads::menuLeds,
#endif
    MCH_PLEIADS, nullptr BUNDLE_REF(pleiads_bundle_ref) },
#endif
#ifdef GAME_GARDIA
  { createMachine<gardia>, gardia::logo,
#ifdef LED_PIN
    gardia::menuLeds,
#endif
    MCH_GARDIA, &gardia_arena_plan BUNDLE_REF(gardia_bundle_ref) },
#endif
#ifdef GAME_CAVENGER
  { createMachine<cavenger>, cavenger::logo,
#ifdef LED_PIN
    cavenger::menuLeds,
#endif
    MCH_CAVENGER, nullptr BUNDLE_REF(cavenger_bundle_ref) },
#endif
#ifdef GAME_VULGUS
  { createMachine<vulgus>, vulgus::logo,
#ifdef LED_PIN
    vulgus::menuLeds,
#endif
    MCH_VULGUS, nullptr BUNDLE_REF(vulgus_bundle_ref) },
#endif
#ifdef GAME_MIKIE
  { createMachine<mikie>, mikie::logo,
#ifdef LED_PIN
    mikie::menuLeds,
#endif
    MCH_MIKIE, nullptr BUNDLE_REF(mikie_bundle_ref) },
#endif
#ifdef GAME_ARKANOID
  { createMachine<arkanoid>, arkanoid::logo,
#ifdef LED_PIN
    arkanoid::menuLeds,
#endif
    MCH_ARKANOID, nullptr BUNDLE_REF(arkanoid_bundle_ref) },
#endif
#ifdef GAME_1943
  { createMachine<_1943>, _1943::logo,
#ifdef LED_PIN
    _1943::menuLeds,
#endif
    MCH_1943, nullptr BUNDLE_REF(_1943_bundle_ref) },
#endif
#ifdef GAME_CBDASH
  { createMachine<cbdash>, cbdash::logo,
#ifdef LED_PIN
    cbdash::menuLeds,
#endif
    MCH_CBDASH, nullptr BUNDLE_REF(cbdash_bundle_ref) },
#endif
#ifdef GAME_PHOZON
  { createMachine<phozon>, phozon::logo,
#ifdef LED_PIN
    phozon::menuLeds,
#endif
    MCH_PHOZON, nullptr BUNDLE_REF(phozon_bundle_ref) },
#endif
#ifdef GAME_GBERET
  { createMachine<gberet>, gberet::logo,
#ifdef LED_PIN
    gberet::menuLeds,
#endif
    MCH_GBERET, nullptr BUNDLE_REF(gberet_bundle_ref) },
#endif
#ifdef GAME_HERO
  { createMachine<hero>, hero::logo,
#ifdef LED_PIN
    hero::menuLeds,
#endif
    MCH_HERO, nullptr BUNDLE_REF(hero_bundle_ref) },
#endif
#ifdef GAME_MARBLE
  { createMachine<marble>, marble::logo,
#ifdef LED_PIN
    marble::menuLeds,
#endif
    MCH_MARBLE, &marble_arena_plan BUNDLE_REF(marble_bundle_ref) },
#endif
#ifdef GAME_COMMANDO
  { createMachine<commando>, commando::logo,
#ifdef LED_PIN
    commando::menuLeds,
#endif
    MCH_COMMANDO, &commando_arena_plan BUNDLE_REF(commando_bundle_ref) },
#endif
#ifdef GAME_GNG
  { createMachine<gng>, gng::logo,
#ifdef LED_PIN
    gng::menuLeds,
#endif
    MCH_GNG, &gng_arena_plan BUNDLE_REF(gng_bundle_ref) },
#endif
#ifdef GAME_TP84
  { createMachine<tp84>, tp84::logo,
#ifdef LED_PIN
    tp84::menuLeds,
#endif
    MCH_TP84, nullptr BUNDLE_REF(tp84_bundle_ref) },
#endif
#ifdef GAME_DEFENDER
  { createMachine<defender>, defender::logo,
#ifdef LED_PIN
    defender::menuLeds,
#endif
    MCH_DEFENDER, &defender_arena_plan BUNDLE_REF(defender_bundle_ref) },
#endif
#ifdef GAME_ROBOTRON
  { createMachine<robotron>, robotron::logo,
#ifdef LED_PIN
    robotron::menuLeds,
#endif
    MCH_ROBOTRON, &robotron_arena_plan BUNDLE_REF(robotron_bundle_ref) },
#endif
#ifdef GAME_JOUST
  { createMachine<joust>, joust::logo,
#ifdef LED_PIN
    joust::menuLeds,
#endif
    MCH_JOUST, &joust_arena_plan BUNDLE_REF(joust_bundle_ref) },
#endif
#ifdef GAME_SINISTAR
  { createMachine<sinistar>, sinistar::logo,
#ifdef LED_PIN
    sinistar::menuLeds,
#endif
    MCH_SINISTAR, &sinistar_arena_plan BUNDLE_REF(sinistar_bundle_ref) },
#endif
#ifdef GAME_YIEAR
  { createMachine<yiear>, yiear::logo,
#ifdef LED_PIN
    yiear::menuLeds,
#endif
    MCH_YIEAR, nullptr BUNDLE_REF(yiear_bundle_ref) },
#endif
#ifdef GAME_TRACKFLD
  { createMachine<trackfld>, trackfld::logo,
#ifdef LED_PIN
    trackfld::menuLeds,
#endif
    MCH_TRACKFLD, nullptr BUNDLE_REF(trackfld_bundle_ref) },
#endif
#ifdef GAME_TRON
  { createMachine<tron>, tron::logo,
#ifdef LED_PIN
    tron::menuLeds,
#endif
    MCH_TRON, nullptr BUNDLE_REF(tron_bundle_ref) },
#endif
#ifdef GAME_DOTRON
  { createMachine<dotron>, dotron::logo,
#ifdef LED_PIN
    dotron::menuLeds,
#endif
    MCH_DOTRON, nullptr BUNDLE_REF(dotron_bundle_ref) },
#endif
#ifdef GAME_ELEVATOR
  { createMachine<elevator>, elevator::logo,
#ifdef LED_PIN
    elevator::menuLeds,
#endif
    MCH_ELEVATOR, nullptr BUNDLE_REF(elevator_bundle_ref) },
#endif
#ifdef GAME_MHAVOC
  { createMachine<mhavoc>, mhavoc::logo,
#ifdef LED_PIN
    mhavoc::menuLeds,
#endif
    MCH_MHAVOC, nullptr BUNDLE_REF(mhavoc_bundle_ref) },
#endif
#ifdef GAME_MEGAZONE
  { createMachine<megazone>, megazone::logo,
#ifdef LED_PIN
    megazone::menuLeds,
#endif
    MCH_MEGAZONE, nullptr BUNDLE_REF(megazone_bundle_ref) },
#endif
#ifdef GAME_JUNGLEH
  { createMachine<jungleh>, jungleh::logo,
#ifdef LED_PIN
    jungleh::menuLeds,
#endif
    MCH_JUNGLEH, nullptr BUNDLE_REF(jungleh_bundle_ref) },
#endif
#ifdef GAME_MOTOS
  { createMachine<motos>, motos::logo,
#ifdef LED_PIN
    motos::menuLeds,
#endif
    MCH_MOTOS, nullptr BUNDLE_REF(motos_bundle_ref) },
#endif
#ifdef GAME_SHOLLOW
  { createMachine<shollow>, shollow::logo,
#ifdef LED_PIN
    shollow::menuLeds,
#endif
    MCH_SHOLLOW, nullptr BUNDLE_REF(shollow_bundle_ref) },
#endif
};

template <std::size_t N, class T>
constexpr std::size_t countof(T(&)[N]) { return N; }
static_assert(countof(machines) >= 1, "At least one machine has to be enabled!");

#endif // _MACHINES_H_

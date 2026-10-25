<!-- markdownlint-disable MD013 -->
# romszip

Put the ROM zips and user files here. No ROMs are included. Names must match exactly.
One line per game: title, then each needed file with the SHA1 of the zip used for testing, then
notes. Every file on a line is needed (parent and BIOS zips, `.sid` tunes). [romconv](../romconv/)
checks the SHA1 of each ROM inside the zips, so other zip builds work.

## Games

* Pac-Man: pacman.zip (dcb0bbc7e7fa5a22269715b79feb9140d4ee1b05)
* Galaga: galaga.zip (bf07d0b87e621ac4d89cf814566ee7d5bcb41978)
* Dig Dug: digdug.zip (b475d8d5cb0df3ef47d57a12aaf95aaebe61c14b)
* Frogger: frogger.zip (f47df182dd106a7abd018cb27c1962fd41bec301)
* Donkey Kong: dkong.zip (7511e3d3dd51dcfbd30ee8e5ba2e7b7fca8a5efa)
* 1942: 1942.zip (bf332ac22c7fdb5470aff4172a9051555e6e1266)
* Lizard Wizard: lizwiz.zip (6b7da891a151eb174408b444ad6998c3eb4bc70e), pacman.zip (dcb0bbc7e7fa5a22269715b79feb9140d4ee1b05)
* Eyes: eyes.zip (3c602e50b1d9b5ae925340577d7ba39db17936a9), pacman.zip (dcb0bbc7e7fa5a22269715b79feb9140d4ee1b05)
* Mr. TNT: mrtnt.zip (6cf0bc43a97cffe5452cdb6cb69ea2c018450bad), pacman.zip (dcb0bbc7e7fa5a22269715b79feb9140d4ee1b05)
* The Glob: theglobp.zip (97aad14ebf4d0ac28f9401b0895dd20a089cd196), pacman.zip (dcb0bbc7e7fa5a22269715b79feb9140d4ee1b05)
* Crush Roller: crush.zip (87646c90c488e0646f3ae82c615eb104dd6ddcc2), pacman.zip (dcb0bbc7e7fa5a22269715b79feb9140d4ee1b05)
* Ant Eater: anteater.zip (ae92624a4cf4fef6781bf5aab54d21dfc861c8fb), frogger.zip (f47df182dd106a7abd018cb27c1962fd41bec301)
* Bombjack: bombjack.zip (8a6a672be241e460fe38d3e5b6d941b4219eea0f)
* Mr. Do!: mrdo.zip (4cc7623052161d858f78bda34f73bacc45413cc9)
* Bagman: bagmanm2.zip (27232ad9b11c60960527c9de675df1e848c06e26)
* Pengo: pengo.zip (05634373f34489bb7985090bd1605ed60a82032c), pacman.zip (dcb0bbc7e7fa5a22269715b79feb9140d4ee1b05) - sets pengoj (preferred, Popcorn tune) and pengo
* MsPacman: mspacman.zip (1438c0c820aac16d25f20ea1e89ed080ae5c09b7), pacman.zip (dcb0bbc7e7fa5a22269715b79feb9140d4ee1b05)
* Galaxian: galaxian.zip (17cf6aa2cfb3a5c9341856a1fc37c830727d3c76)
* LadyBug: ladybug.zip (766b510749149f5c3e79576fb03c0280fefb7a74)
* Space Invaders: invaders.zip (2e971b48fcada68eedd69f94ab0924c48e9e557a)
* Time Pilot: timeplt.zip (c238667e83a222070c12192ce78d6a1d0b1cd59b)
* Gyruss: gyruss.zip (88db6d3d613f472211f89fc1d7a5506e937a6a07)
* Tutankham: tutankhm.zip (122dcc7d9380a2673879204c9489a7792c483b64)
* Donkey Kong Jr.: dkongjrj.zip (4751e9a74671a4b9519223afbada6f2636e39a38), dkong.zip (7511e3d3dd51dcfbd30ee8e5ba2e7b7fca8a5efa)
* Star Force: starforc.zip (d03ce89a6ea1ad4080359d9df5e6094a2d480580)
* Moon Cresta: mooncrst.zip (8c042195b606e17302bbf81d58eeefbf2be1643f)
* Scramble: scramble.zip (1130ddac44d2bc56800b9e7c48fd0ad14e9c0206)
* Super Cobra: scobra.zip (051ec8aa6a43b1853ea1b73968d3db55cb10a9bd), scramble.zip (1130ddac44d2bc56800b9e7c48fd0ad14e9c0206)
* Donkey Kong 3: dkong3.zip (d1e6b9d923c0094387671dd865002004094cbc16)
* Pooyan: pooyan.zip (c19efee37dd8d3734a77a8623bd24610071fe7f0)
* Phoenix: phoenix.zip (5a6b39a66872940fb5c83e691c681ef908784695)
* Burger Time: btime.zip (22c3416c76fb1e2cf38f88e655b3d5dbc8bda72f)
* Xevious: xevious.zip (12735ab7ab5ac1b7a56874d1afe4fbbdf79a7ed5), galaga.zip (bf07d0b87e621ac4d89cf814566ee7d5bcb41978)
* Bump 'n' Jump: bnj.zip (864965a00336ed3d18dd520b98170e1bddab7929), btime.zip (22c3416c76fb1e2cf38f88e655b3d5dbc8bda72f)
* Mappy: mappy.zip (7a0f0c6ecabf996c427a7bcf98702ec746c90cdb)
* Gaplus: gaplus.zip (f665cda19716eebf9429bd99e7451b8ef7b3d172), gaplus-samples.zip (9b79ea9a9e22e054ab6d04117588d0d3da1b2e10) - gaplus.zip may be galaga3.zip; gaplus-samples.zip: MAME samples `gaplus.zip`, renamed ([progetto-SNAPS](https://www.progettosnaps.net/samples/))
* Alibaba: alibaba.zip (cc61cdd716fc9377487b29e30b24cc43d38a794f), pacman.zip (dcb0bbc7e7fa5a22269715b79feb9140d4ee1b05)
* Amidar: amidar.zip (8e7a8ba1062c6e0fc164e4daa9974f377695f679), turtles.zip (09593e6137a13415c2af0b59c08129309e0f6e16), scramble.zip (1130ddac44d2bc56800b9e7c48fd0ad14e9c0206)
* Turtles: turtles.zip (09593e6137a13415c2af0b59c08129309e0f6e16), scramble.zip (1130ddac44d2bc56800b9e7c48fd0ad14e9c0206)
* Circus Charlie: circusc.zip (98f6e9934a3d9832845d9cad6291ca97faef460e)
* Roc'n Rope: rocnrope.zip (c081576af46aa5db220d9602923ac57918e02928)
* Tower of Druaga: todruaga.zip (bd659a5455e76151a9e37317b504ab36a8276450)
* Van Van Car: vanvan.zip (dc8f8781c4f80151fd19280b823d13ca145340db)
* Pinball Action: pbaction.zip (ebb6b130db7575a84e34ee0daa8aa762f2e62602)
* Fantasy Island: fantasyu.zip (2b9b6233aa6d161dc7a8ccb5d1945d3470ce34aa), fantasy-samples.zip (f2a605f570a5c7cf18777002383a5c116c658b3d) - fantasy-samples.zip: HBMAME samples `fantasy.zip`, renamed ([progetto-SNAPS](https://www.progettosnaps.net/samples/))
* Nibbler: nibblerp.zip (02d05e34d42c6b66397ad49f67facebe14029699)
* Vanguard: vanguard.zip (5fe916103fc56afdc8ae0c17d3dd6c358d78e4f2), vanguard-samples.zip (e3de9c60e6f972dcaef5d251813438c6151aa8b7) - vanguard-samples.zip: HBMAME samples `vanguard.zip`, renamed ([progetto-SNAPS](https://www.progettosnaps.net/samples/))
* Scrambled Egg: scregg.zip (5549a14d93e702b8763a8f8edb293f2bb4ad7fcf)
* Road Fighter: roadf2.zip (1207de1911ff0e61ccf527b050e6add7aad7f6c5)
* Motorace USA: motorace.zip (486cd3b41bd6cd5980d8b7f99c982dc0f39b290b)
* Zaxxon: zaxxon.zip (eed2fbc9baf6eff54038a0e789cfc01789e55423), zaxxon-audio.zip (799fbc97a6d01fed9bc57a406e1d9824ee573caf) - US Rev D; samples in zaxxon-audio.zip
* Centipede: centiped.zip (7b00b0b523ec0f07b9b28be0c616ca7d7f9190db)
* Millipede: milliped.zip (4c9a07f4df373c25d560601dd743e0713df04d4a)
* Asteroids: asteroid.zip (18beaef2089e5135c89c25ce702b4ec7c6284684)
* Baluba-louk no Densetsu: baluba.zip (de9bfd1291768f84ebf3f7fdaecb345a615882fe), starforc.zip (d03ce89a6ea1ad4080359d9df5e6094a2d480580)
* Commando: commando.zip (151e4741f0364aca5298c2ad776ec30a8e3d1422), Commando.sid (8549166918b51bfde88f3f059588c4618dfbfc75) - Commando.sid: HVSC Hubbard_Rob/Commando.sid
* Lunar Lander: llander.zip (bf405fe9f30cbfa45a412c06c9ff934913e333f7) - Rev 2
* The Pit: thepit.zip (5816da50668a0c4d164a310dfb85ab6996ba5e2f)
* Bubble Bobble: boblbobl.zip (004b5e3bf89482b5a4728d96e30609a91e248d7c), bublbobl.zip (f26ecd6b57f0c8938a13dfd8079fe6d5a658773f), Bubble_Bobble.sid (d9af85791a8cdd742ab1c4d567b54ac0e8d7b2b7) - bootleg "Bobble Bobble" (no MCU), parent bublbobl.zip; Bubble_Bobble.sid: HVSC Clarke_Peter/Bubble_Bobble.sid
* Tetris: atetris.zip (bdd64b06496b97feda1166504d8e5d9a9e5d4fd6) - Atari set 1
* Crystal Castles: ccastlesj.zip (132791b0b2bf59f31d2af01ca175ae10586e8129), ccastles.zip (2da8e860840da1d4fe9a0fee767154f191126010) - joystick set, parent ccastles.zip
* Battle Zone: bzone.zip (59a89eaecb70730088062fd2826853c0a766b2b0) - Rev 2
* Tempest: tempest.zip (4759782ab48e972ace2816c32487d605fe801138) - Rev 3
* Space Duel: spacduel.zip (652190914d0d1336bcb07d276eadd6561f9f3564) - Version 2
* Berzerk: berzerk.zip (29c31ad5407690d1be90498b58b7888f68391c5c) - Revision RC31A
* Gravitar: gravitar.zip (4ea837062655fa92fba980dcd2e578a48bc73036) - Version 3
* Marble Madness: marble.zip (603cb95ee7269f849a2b3ef8afd7cadfd5f3799b), atarisy1.zip (0c9bc4b948bd1816ebe13e2acb97931cbcc6ea15), Marble_Madness.sid (cd2ef568f66df9f9ba3d1d1b47971cf1a16c15e7) - atarisy1.zip is the BIOS; Marble_Madness.sid: HVSC GAMES/M-R/Marble_Madness.sid
* Star Wars: starwars.zip (7c12ddf119fe7253fe4072609b8dbc3bb75ed67c) - Set 1
* Missile Command: missile.zip (9e9385e92bd8283794a6ea9c3e28095f80a8043e) - Rev 3
* Moon Patrol: mpatrol.zip (156805e5610bc7d016f8990acfd0a0b821db100c)
* Star Jacker: starjack.zip (fd69548f3a6dc67727ad77f734ccb06b64b1c8b5)
* Regulus: regulus.zip (8d4c981af9d477a79bc6239c99784a7ba164501d) - unencrypted US set
* Boulder Dash: cbdash.zip (0c0f7ff29cbe1caa5a22aa0cf291bf5560ddd358), decocass.zip (1b33cf0e730a6cde96479b0face108b6931d433e), Boulder_Dash.sid (2d795692e2c2ccd99dba58e85da487086923b329) - decocass.zip is the DECO Cassette BIOS; Boulder_Dash.sid: HVSC Liepa_Peter/Boulder_Dash.sid
* Up'n Down: upndown.zip (76698c100efd6a84a1045abe8981cee27aea7e32) - set upndownu
* Phozon: phozon.zip (24e52f7f047e84366c4b9efdb45d593a95f61673)
* Green Beret: gberet.zip (014968a2bb0952288069772bad51ae8325823595)
* H.E.R.O.: H.E.R.O..zip (848835ac6a02e72fa692c22bc7f332fc71fcf790) - Atari 2600 cartridge ROM; or H.E.R.O.-pal.bin (PAL cartridge dump) in its place
* Ghosts'n Goblins: gng.zip (56d141c7726cb326bf160b96af6a73fd120c756b), Ghosts_n_Goblins.sid (2ee5606a190208b4ea424bd7152c28148af62d51) - Ghosts_n_Goblins.sid: HVSC Cooksey_Mark/Ghosts_n_Goblins.sid
* Time Pilot '84: tp84.zip (82e52144b0797b41b3dee11f13836bb066453a6a)
* Defender: defender.zip (c5a6bcfbe02ad108143a5e2198ccb30ed22abfdd)
* Robotron: 2084: robotron.zip (45fd1c752eee8842bbb1e901f3f559eff524630c)
* Joust: joust.zip (670aea785c0ce57f8626db4036840755a13c7eac)
* Rafflesia: raflesia.zip (b572b221cc62f3e9b1d0f5a393504ca2688d08e1)
* SWAT: swat.zip (994089f516f0d1b6a667e4bb5d874cb62d46c654)
* Black Widow: bwidow.zip (0572f87c70cb2a3bde1261d9bf0f22de78160f53)
* 1943: 1943.zip (1f5fd08c18b882d2978539756543c7660c82c14e) - bootleg set 1943b from the merged zip
* Dig Dug II: digdug2.zip (858f9dfdedf69b4b0b881840d670c7e22645b58c)
* Gorf: gorf.zip (f9f2275983156a177129ed7521c37b4c7f5df5ec), votrsc01.zip (a57f14469867ce0d0865995a645b69f41a0ea718) - votrsc01.zip is the Votrax speech ROM
* Solomon's Key: solomon.zip (933a095ee62563f538dec716768a5466e09f022f)
* Flicky: flicky.zip (bb72956cc4fdbc2a0be48d15a56831d922d6270b) - set flickys2
* Pitfall II: pitfall2u.zip (fc1dd77dec266fd7e9e0331946c51ad4749639f2), pitfall2.zip (7520eccc04220698cd31264e0839a0f3894ef793) - unencrypted US set, parent pitfall2.zip
* Mister Viking: mrviking.zip (31887281d9ac56221302fb7dfa280c52e7350a2d)
* I'm Sorry: imsorry.zip (ab5aa9e7e49d3a09e004672b070e60bdcc525879)
* Wonder Boy: wboy.zip (521221e1d93fb10d23e99ada4bd26f0edf58e55c) - set wboy2u
* 4-D Warriors: 4dwarrio.zip (136232868bd1e6640c6d04e889e36214dc5cb149)
* Heavy Metal: hvymetal.zip (d375dec88ed5b37ea7c3136ba96a1da82efddd6b)
* Super Xevious: sxevious.zip (894afa1d525a7ddc015f916ac85435a8b91f061f), xevious.zip (12735ab7ab5ac1b7a56874d1afe4fbbdf79a7ed5), galaga.zip (bf07d0b87e621ac4d89cf814566ee7d5bcb41978) - parent xevious.zip
* Mikie: mikie.zip (7dd34b5d63af4c46d491775413dbeb4aa5fb1d2d)
* Vulgus: vulgus.zip (dd7858ed73a8acd6b1ad75069c812330529264a0)
* Cosmic Avenger: cavenger.zip (b1d08f3d617724add779630224abd4bb99088c64)
* Gardia: gardia.zip (aa47a16988fb51488021d2f19f6f07113ef3c5b3)
* Pleiads: pleiads.zip (5c72d6d7ef71068d331a55488d6a1f48ce68d916)
* Arkanoid: arkanoid.zip (ce311259ccac25da8c27ff219b6f966485a51920) - bootleg set arkangc (no MCU)
* Sinistar: sinistar.zip (a51551f2b9930bbbbdb5d6b71056182553125f20) - Rev 3 upright
* Yie Ar Kung-Fu: yiear.zip (12284f829f9d84a5abc12d8a895be8d4701ab534)
* Track & Field: trackfld.zip (1086b43e2e03be9c030413a287bc7bf594b5333f)
* Tron: tron.zip (6507001eb96f1cbaf0e70dec3626d25baf0e5083), midssio.zip (54275c9833e497f71f76ab239030cc386c863991) - midssio.zip is the sound board
* Discs of Tron: dotron.zip (3722fdcf8aecb6ce77b8024cebf962bbb8c32607), midssio.zip (54275c9833e497f71f76ab239030cc386c863991) - midssio.zip is the sound board
* Elevator Action: elevator.zip (2081eab67c624ec8dd486c56852917e15cd5e3ca) - bootleg set elevatorb (no MCU)
* Major Havoc: mhavoc.zip (00fff6f23e73925fd3552c0b80f427ed2aafab1b) - Rev 3
* Satan's Hollow: shollow.zip (8955d419894fe60f4f67972439ebae08dba779da), midssio.zip (54275c9833e497f71f76ab239030cc386c863991) - midssio.zip is the sound board
* Motos: motos.zip (7498a168d5e6e5b0185219bf3b8ad8fe7d4cfba3)
* Jungle Hunt: junglek.zip (74072d8f71cc796bd6892f2848bdc83363cebec8) - set jungleh (US)
* Mega Zone: megazone.zip (e6fd0c4af0d0365dc581ef64be88961eb0c19912)
* Choplifter: choplift.zip (66acea8fae0583738607d92ac8ba40633834fc2e) - set chopliftu
* Toki no Senshi (Chrono Soldier): tokisens.zip (f3a8bd4cdf1097dda102244e9f32753e4cd52780)
* Tapper: tapper.zip (e5d340610f477620e2eb42f989628c1fddc9f974), midssio.zip (54275c9833e497f71f76ab239030cc386c863991) - set Budweiser set 1; midssio.zip is the sound board
* Mario Bros.: mario.zip (39b6f538d0172adf0f9fa675968182ba64c4e8f7)
* Mr. Do's Castle: docastle.zip (10431e063057d1fee95980070e0ae1ec7059045c)
* Super Zaxxon: szaxxon.zip (b2a7c1cc98c3ff7afcdb377535fc6da4032eb717), zaxxon.zip (eed2fbc9baf6eff54038a0e789cfc01789e55423), zaxxon-audio.zip (799fbc97a6d01fed9bc57a406e1d9824ee573caf) - shares the Zaxxon conversion
* Contra: contra.zip (20f9644b22773cc1e45f8bf18750faf54cd1884c), Gryzor.sid (337bfa29401763bcb0f0d0cecd8db7bb4ed94d52) - Gryzor.sid: HVSC MUSICIANS/B/Brooke_Jason/Gryzor.sid
* Mr. Goemon: mrgoemon.zip (cc940cefcaf967c3ed9b24b43ff6083c933efcf6), gberet.zip (014968a2bb0952288069772bad51ae8325823595) - parent gberet.zip
* Jail Break: jailbrek.zip (ab7a61e7b644f005e54f2c173dbb4ae59b2d8af9)
* Scooter Shooter: scotrsht.zip (cb5aa7a961a60df8d124b8eeb8c6c79d06488b37)
* Q*bert: qbert.zip (6fd762ba09b779e9708fec75a59bf899cbf95e49), votrax-qbert.zip (14a5ff2c8ca68d35a77ebfbc227ed9c8e19d7524) - US set 1; votrax-qbert.zip is the Votrax speech ROM
* Krull: krull.zip (808e941967e80294edfe76d47e501e0e17526c91)
* Juno First: junofrst.zip (d9aac3753859e21f711117e2906aa74eb1c0ce64)
* Tylz: tylz.zip (f74ee0a7ce2f05dd54f4fbf754c233a2098ba6dc), votrax-qbert.zip (14a5ff2c8ca68d35a77ebfbc227ed9c8e19d7524) - prototype; votrax-qbert.zip is the Votrax speech ROM

## CPU cores and tools

* Z80: Z80-081707.zip (3f6f1cefee313f9b44eec13be79f4f84077d752e) - Marat Fayzullin's Z80 emulator. Needed by every build. The `z80` job unpacks it into `source/src/cpus/z80` and patches it.
* 6502, 6803, 6809, 8048, MB88xx, 8086: nothing to download, the cores are in `source/src/cpus`.

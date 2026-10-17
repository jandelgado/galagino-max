#!/usr/bin/env python3
"""Convert a Sega System 1 ROM set into galagino headers (MAME sega/system1.cpp)."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "pyconv"))
from gfxutil import load_file, rgb888_to_rgb565_le
from arena_layout import LOCAL
from asset_emit import emit_compressed, emit_plain
from resnet import resistor_weights, combine

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..", "..")
MACHINES = os.path.join(ROOT, "source", "src", "machines")

ARENA_BLOCKS = LOCAL
MIX_NO_COLLIDE = 0x04

# MAME ROM_START per game, regions in load order
GAMES = {
    "starjack": {
        "main": [("epr-5320b.129", "28d3f87851cccc94a86eb0217893de0baf8e62fd"),
                 ("epr-5321a.130", "79fd23bb7db577d1dbf1b50503085eccdd17b98c"),
                 ("epr-5322a.131", "6bb8a063279c93341ff472351b79c92795845f74"),
                 ("epr-5323.132", "6cfa288e28e3b415db5ef3cef8e8849259234af9"),
                 ("epr-5324.133", "d36ef8f176d5e44884d3d0b9af81be6f7fbd0cde"),
                 ("epr-5325.134", "27362b3e10d9d4235647eadb9cd1404700a8fb49")],
        "sound": [("epr-5332.3", "4a6ad09949a438562d7043532d628cefdbb5a2fe")],
        "tiles": [("epr-5331.82", "353067a75d583d5f53ce2f473b52a35dd912639f"),
                  ("epr-5330.65", "a2e90d20a07608f43273e84d1eb22f195b19626c"),
                  ("epr-5329.81", "6d19543427b9c4d8d8f5ea0872cdf8cc4fe5018c"),
                  ("epr-5328.64", "cc30d144ff70539bbc82c848c154e156a33b638c"),
                  ("epr-5327.80", "03124ce123684b8469cf42be6ed5f0fffa64c480"),
                  ("epr-5326.63", "bc7528456fe8dee9aa21212aa996fc347c5d55b4")],
        "sprites": [("epr-5318.86", "326d538551245fce67d0fdba85975e27093b7a93"),
                    ("epr-5319.93", "bb331be270dc1da63699533d9f02d73ce28f1bc5")],
        "lookup": [("pr-5317.106", "c7986aa9127ef5b50b845434cb4e81dff9861cd2")],
    },
    # regulusu: unencrypted set, files in subdir regulusu/ of regulus.zip
    "regulus": {
        "main": [("regulusu/epr-5950.129", "0164cb919a50013f23568f59caff19ff2d0bf11f"),
                 ("regulusu/epr-5951.130", "9e339c716c646bd02bedbe27096b75f633554e7c"),
                 ("regulusu/epr-5952.131", "9b6d8b2e0a0bec45bfbb9f8ccc728e18e909685f"),
                 ("regulusu/epr-5953.132", "1e051595aff34db06186542bcfc3849bc88eb5d4"),
                 ("epr-5644.133", "6fe471548d227d834c012d5d148b1ea1c12dfd00"),
                 ("regulusu/epr-5955.134", "4f94eaac900da5ca512289e2339776b1139e03e1")],
        "sound": [("epr-5652.3", "bc181c73a6009ca723e715650adb920b77bd311c")],
        "tiles": [("epr-5651.82", "f86acf2de639ac89f80cdf627d1d6b5f5e4f1557"),
                  ("epr-5650.65", "27ba8e2bb820913e58cb029da9c18d35e67728b8"),
                  ("epr-5649.81", "28f74bcf1e6bc06db0984dcf86dd527e301b0c01"),
                  ("epr-5648.64", "1ee18562250468f8f09a3062705422c28c740674"),
                  ("epr-5647.80", "2c1be04de6ec652ea8a566eb0eb1a9bcb4c90e66"),
                  ("epr-5646.63", "e34d1e1aaf3ae7a138e75df5dedebfb4acd79340")],
        "sprites": [("epr-5638.86", "c8024f541086a8a940e21219fa4522646aeb365a"),
                    ("epr-5639.93", "033bf46d2625f99544a784fe3fa299cc1b1b48e1")],
        "lookup": [("pr-5317.106", "c7986aa9127ef5b50b845434cb4e81dff9861cd2")],
    },
    # upndownu: unencrypted set, files in subdir upndownu/ of upndown.zip
    "upndown": {
        "main": [("upndownu/epr-5679.129", "7904ffb46a2c3ef69b9784f343ff37d81bbee11d"),
                 ("upndownu/epr-5680.130", "14cc846f03b71e0922689388a6757955cfd88bd8"),
                 ("upndownu/epr-5681.131", "440dc8c18183612c32486c617f5d7f38fd804f0e"),
                 ("upndownu/epr-5682.132", "71f06d1c4a580fed07ad32c6d1f2d37d47ed95b1"),
                 ("epr-5520.133", "eff0c91ce6c2c1f6e191bcbf9ae83dd377cbb408"),
                 ("upndownu/epr-5684.133", "ebe87d28dde6b8356d40572e9f2cd35ec240075f")],
        "sound": [("upndownu/epr-5528.3", "7f5385aa0773681329a4759b0fa6f975e3de6755")],
        "tiles": [("epr-5527.82", "c079136a5d73e1d55ddbad6efb5e7067d0ff412b"),
                  ("epr-5526.65", "db796d5c4ab3f749287133eaf05818f89dc2afb7"),
                  ("epr-5525.81", "2022cbd42ff0177cdd661bb00b1004459b6af83a"),
                  ("epr-5524.64", "6a9c909d10ccb03a8af6fa9d8067946d60b91592"),
                  ("epr-5523.80", "6c47fa433e16ccc3fff9347a4fe8f0165d20a3d2"),
                  ("epr-5522.63", "4f18274f5dc349b99b3daec517ccf5ccbb932d1c")],
        "sprites": [("epr-5514.86", "ca7db3df10deb6720096e6c50eddd9b74c47f0a0"),
                    ("epr-5515.93", "aedff8ce07ab16942037e5aff212652e51c19e71")],
        "lookup": [("pr-5317.106", "c7986aa9127ef5b50b845434cb4e81dff9861cd2")],
    },
    # raflesiau: unencrypted set, main ROMs in subdir raflesiau/ of raflesia.zip
    "raflesia": {
        "main": [("raflesiau/epr-7433.129", "4c02e052c01099478adb5bb595c035aff862dd45"),
                 ("raflesiau/epr-7434.130", "7bba493c8493611952e029dc8782916cc25db82a"),
                 ("raflesiau/epr-7435.131", "5bffdcdf543f47ccf2ed0cabf1ef9e42857d59f9"),
                 ("raflesiau/epr-7436.132", "aecf33c777e8f4a7323d72dd3f2ca5d12cc798e7"),
                 ("raflesiau/epr-7437.133", "a97654ff82ffa051ee14359733c2b9a952134d4e"),
                 ("raflesiau/epr-7438.134", "c353ed4d21661ea0721d8b85d6bfefe502373d7b")],
        "sound": [("epr-7420.120", "9cb18e3002c32f658e4725707069f9cd2f496507")],
        "tiles": [("epr-7419.62", "78c4d380d5558212e535c3262223137447d64818"),
                  ("epr-7418.61", "48be9337f704a11ac1fdeb64a3b3518c796bcdd0"),
                  ("epr-7417.64", "5cfd19241c54782c262bbb23c6f682534e77feb7"),
                  ("epr-7416.63", "78bb1c4651bd63a9f776766d2eac4f1c09242ed5"),
                  ("epr-7415.66", "b04ee35f603c6c9923ba888914eb43a8b7753d92"),
                  ("epr-7414.65", "bdc0185d133f7bbe287106882bacde846634ffa4")],
        "sprites": [("epr-7407.117", "c6f06144b708055b31fbcba9f38b63736db789d8"),
                    ("epr-7409.04", "e63f0422814423be91d8e1937a13d19693a1a5fc"),
                    ("epr-7408.110", "8476c2c01920f0492cf643929d4f023f3afe0164"),
                    ("epr-7410.05", "d0ad845bfe83412ac8d43125e1c50d0581a5b47e")],
        "lookup": [("pr-5317.76", "c7986aa9127ef5b50b845434cb4e81dff9861cd2")],
    },
    # swat: 315-5048 encrypted main CPU (0000-7fff), decrypted here
    "swat": {
        "main": [("epr5807b.129", "56e9d9a33f04b4d5971c0db24cc8719a52e64678"),
                 ("epr-5808.130", "e8aa72f2835d38367be5e8a9313e51b64f452ee7"),
                 ("epr-5809.131", "a0b4f0c2e537bd16f7345590da00f2622947d7e4"),
                 ("epr-5810.132", "e740cbe239d379705fdffb3e500d6f5a2fece2e2"),
                 ("epr-5811.133", "abf1f23dc26a7518357d0c1749e869b539c3bbed"),
                 ("epr-5812.134", "adc8dcf643d8d0b0a1d0dda0494567263ea11a00")],
        "sound": [("epr-5819.3", "06062648b9ebc70b4b5c30b043f537adc0052047")],
        "tiles": [("epr-5818.82", "ad217cd8dad178f3f2f1fd44a58adcc4887fb6b7"),
                  ("epr-5817.65", "da7378e8d12cc2970df2efa075c944c79b3b74d2"),
                  ("epr-5816.81", "78ae13a38d6368e44ba95642cce7f5515a5b6022"),
                  ("epr-5815.64", "a0b97e016e5cf43f223ecb6c5fe7dec7c8e9c098"),
                  ("epr-5814.80", "bbb0afe2b195d60418014c36acf3de95adfd90d8"),
                  ("epr-5813.63", "06b1d23cacfef3017e5951dc10e8471e9b3103d5")],
        "sprites": [("epr-5805.86", "55c54e54f052187ddd957131e56400c9c432a6b2"),
                    ("epr-5806.93", "e4e9f929ab8ae7da74f885481cf94335d7553a1c")],
        "lookup": [("pr-5317.106", "c7986aa9127ef5b50b845434cb4e81dff9861cd2")],
        "crypt": "315-5048",
    },
    # flickys2: unencrypted 128k set, sys1pio board, ROT0
    "flicky": {
        "rot0": True,
        "main": [("flickys2/epr-6621.bin", "e1d5438eaf0efeaeb4687dcfc12bf325e804182f"),
                 ("flickys2/epr-6622.bin", "e5e620797daace0843a680cb4572586b5e639ca0")],
        "sound": [("epr-5869.120", "fe02a7a94a1ad046fc775a7f67f460c8d0f6dca6")],
        "tiles": [("epr-5868.62", "5bd660ac24a2d0d8ad983e948674a82a2d2e8b49"),
                  ("epr-5867.61", "4bc3bc6eb8f03926d3710c9f96fcc1b116e918d3"),
                  ("epr-5866.64", "652be7848526c6e61db4a502f75d1689d2ff2f59"),
                  ("epr-5865.63", "e158db3e0b86f2b8ad34cefc2714cb0a942efde7"),
                  ("epr-5864.66", "cf59f1fb0f9fb77d5ac36be52b6ee946ee85d6de"),
                  ("epr-5863.65", "8c629a853486bbe049b1deecdc00f9e16b87698f")],
        "sprites": [("epr-5855.117", "2c72dc16739dad155fcd572e1add067a7647f5bd"),
                    ("epr-5856.110", "dcbfce550d10a1f2b3ce3e7e081fc008cb575708")],
        "lookup": [("pr-5317.76", "c7986aa9127ef5b50b845434cb4e81dff9861cd2")],
    },
    # pitfall2u: unencrypted set, sys1pio board, ROT0. pitfall2u.zip holds
    # only the main CPU ROMs, the rest merges from the parent pitfall2.zip.
    "pitfall2": {
        "rot0": True,
        "main": [("pitfall2u.zip", "epr-6623.116", "d33421999f899c0a4dc0d4553614c1f5c7027257"),
                 ("pitfall2u.zip", "epr-6624a.109", "4869ca4d3f0b08cd3df4c82be9cfc774ddeb3010"),
                 ("pitfall2u.zip", "epr-6625.96", "62fffff7d935c104def5f09e9dc4a26fa4ce4f94")],
        "sound": [("epr-6462.120", "89add2e3784e8f5a20b895fb2c4466bdd6c34b0c")],
        "tiles": [("epr-6474a.62", "c652010a8b19828f81fd101aa1ea781e250c4ec2"),
                  ("epr-6473a.61", "23e04589f2b523d6b8e46d16f40e59685e27f522"),
                  ("epr-6472a.64", "b7a96a1867f8bd3cc1251b5fd12991c406e62a37"),
                  ("epr-6471a.63", "520afa7617e8dfd09bf469c01ac606a4a3acdc5e"),
                  ("epr-6470a.66", "54ea6ef54be6dcc2a5d00f7f817fd8836a02b3b9"),
                  ("epr-6469a.65", "f95e3b368c2c6dbf8265fb314d73019fe7dcce22")],
        "sprites": [("epr-6454a.117", "e0571f6fd031bbe2d971c3be7b96a017b0ea4be9"),
                    ("epr-6455.05", "21743f78735fc9105fbbfac420bdaa2965b4b56f")],
        "lookup": [("pr-5317.76", "c7986aa9127ef5b50b845434cb4e81dff9861cd2")],
    },
    # mrviking: 315-5041, sys1ppis (240 px visarea)
    "mrviking": {
        "crypt": "315-5041",
        "main": [("epr-5873.129", "70e185d03e782be908e6b5c6342cf6a7ebae618c"),
                 ("epr-5874.130", "c2200e0c2f322a08af10e9c2e9191d1c595801a4"),
                 ("epr-5875.131", "11568db9fbca44013eeb0035c0a0a67d6dd18d00"),
                 ("epr-5876.132", "7b1aa19a16fb44f6c69cf053e2e10e5179416796"),
                 ("epr-5755.133", "9648f1ae3033c30ed8ab8d9c87b111756dab7b5e"),
                 ("epr-5756.134", "a0904d19d06fb5ef5eb6da0dc4efe556bc29b33e")],
        "sound": [("epr-5763.3", "8393dfb57d9af22b3280ecaef736b6f9d856dbee")],
        "tiles": [("epr-5762.82", "4687ecc4061719fca5f85b2b290ebb7ced15ee5b"),
                  ("epr-5761.65", "a7a992f52406413e931945be60b35175f8aea6c2"),
                  ("epr-5760.81", "d1848fc4f3d66603d0e8217373a37148aa2eeef5"),
                  ("epr-5759.64", "6fff6086a96be6aa28bec05d1c94c257bb29ef1e"),
                  ("epr-5758.80", "d38ca7c6f36db6e35a3ce87bacdd70f293f23104"),
                  ("epr-5757.63", "c54a1fa02e312676658d7c5392a5a841bdb15d44")],
        "sprites": [("epr-5749.86", "3f626f3e5e2db486ccf727e9869ab488643b4a8c"),
                    ("epr-5750.93", "f246afee7e73bc30054b0e5dcb83fa0edd2d2164")],
        "lookup": [("pr-5317.106", "c7986aa9127ef5b50b845434cb4e81dff9861cd2")],
    },
    # imsorry: 315-5110, sys1pio, ROT0
    "imsorry": {
        "crypt": "315-5110",
        "rot0": True,
        "main": [("epr-6676.116", "b9bcc76bbdfa597d252e7db60fa0f7529e884cce"),
                 ("epr-6677.109", "ad9c722fde08f48d8bc835b244450b01a3d747c2"),
                 ("epr-6678.96", "3395fb769c79f048d099e2898bb7a15611b006c0")],
        "sound": [("epr-6656.120", "a0267d6533af6ff5bf76b9858f2913821a915baf")],
        "tiles": [("epr-6684.62", "abcabdecee0ce52000dab831ae1e50fe12c97066"),
                  ("epr-6683.61", "99c0d141eb5519c31b194693a1fe9be882cb03fd"),
                  ("epr-6682.64", "6aed392a5dd639c54bf54acd3651a77274c0a277"),
                  ("epr-6681.63", "57999a73511b2b3f52d7d6a32addc0641255d7b1"),
                  ("epr-6680.66", "fa2c7df33c685e48020ccabcfba5830e7609e392"),
                  ("epr-6674.65", "e35f6fae7feb9a353321d8239ac8990bc773e60b")],
        "sprites": [("epr-6645.117", "5a105cc3112f2533e7c5982233405d365402fba2"),
                    ("epr-6646.04", "eef7dcde632787283c4cb522380b138060018204")],
        "lookup": [("pr-5317.76", "c7986aa9127ef5b50b845434cb4e81dff9861cd2")],
    },
    # wboy2u: unencrypted, sys1ppi, ROT0. Main ROMs spread over the clone
    # subdirs of wboy.zip (wboy2u, wbdeluxe, wboy2 share files)
    "wboy": {
        "mame_set": "wboy2u",
        "rot0": True,
        "main": [("wboy2u/ic129_02.bin", "e57b7b6818f12fdd5f1600ed54c0b8a7f538aa71"),
                 ("wbdeluxe/ic130_03.bin", "c58c220aa0d0e194581646e6db2491075fdc37b9"),
                 ("wboy2u/ic131_04.bin", "073f8f70685913736eb04be8215a47b5253cb531"),
                 ("wbdeluxe/ic132_05.bin", "c2e1f67b756f558d6904fe82d6f5483cda5f9045"),
                 ("wboy2/epr-7591.133", "da47e0150b724a00878ef5f953fa6ac80bb27d8d"),
                 ("wboy2/epr-7592.134", "a4e8d6a8278ac2227bde8c24d45aa7ab2a273579")],
        "sound": [("wbdeluxe/epr7498a.3", "d2d5cd154ce6a5a3c6a099b4ab2ea7cc045ab0a1")],
        "tiles": [("epr-7497.62", "11799e9ef7e6942b304f132b404bff3ed44d524b"),
                  ("epr-7496.61", "21826aebf5835b9f3d9c467c8647809c1bc0d01f"),
                  ("epr-7495.64", "8c21d7f0768e8dda2b7185f3c510cae4229a4a2e"),
                  ("epr-7494.63", "a88b80a7b94ab1401bbf28d7707fdf28a5505127"),
                  ("epr-7493.66", "7a5098624769a31e7512f56831e818bce6a18871"),
                  ("epr-7492.65", "f91e5868a455dff2bce3c2891a7cfd648957cd73")],
        "sprites": [("epr-7485.117", "e4e11c0e9bd0dc121c25349493f2b13d2ff8c807"),
                    ("epr-7487.04", "d70440a8703ccface3212cd9544c950b36263e8c"),
                    ("epr-7486.110", "9a76a50204c618347d3e8eee6cda841becd906eb"),
                    ("epr-7488.05", "c2f1376144a49d20cb35384648e06d06978474c1")],
        "lookup": [("pr-5317.76", "c7986aa9127ef5b50b845434cb4e81dff9861cd2")],
    },
    # 4dwarrio: 315-5162 (segacrp2), sys1pio, ROT0. Key is a C identifier
    # (asset names), the MAME set and zip are 4dwarrio
    "fourdwarrio": {
        "mame_set": "4dwarrio",
        "zip": "4dwarrio.zip",
        "crypt": "315-5162",
        "rot0": True,
        # Arena total 218656 > 201500: the opcode image (32 KB, read per M1
        # fetch from flash cache) stays PLAIN
        "plain": ["opcodes"],
        "main": [("4d.116", "724bb2f77a2b82fae85e535ae4a37820cfb323d0"),
                 ("4d.109", "bc368abeb6c0a7172e03bd7a1754cf4a6ecbb4f8"),
                 ("4d.96", "520ecbafd1c7271dad24410a68067dfd801fa6d6")],
        "sound": [("4d.120", "b7a21f95b63234f2496d5ea6e7dc8050ca1b39fc")],
        "tiles": [("4d.62", "fdc288769495f4b0ca8c7594c9ab7dc0f29e57a4"),
                  ("4d.61", "55f92309223c41871175b1f54418c8b08339deb0"),
                  ("4d.64", "07076ef66e29c730050e38aecabdfbfced9f9bc4"),
                  ("4d.63", "c09c1df4f04608381d665a83776005607ad97ad4"),
                  ("4d.66", "ea1c18d07957301f2006350b02fe40d13dbe2aa5"),
                  ("4d.65", "ba2581a52eb54e2d9f1e1bf30050280df3f5df1b")],
        "sprites": [("4d.117", "2574d5c3b01c89d8a041c82af976147d3b87b36b"),
                    ("4d.04", "4851754cb56784ac248f699f0781646455dd556b"),
                    ("4d.110", "a26dbd470744c38a26a016e5d4792ac2f2b9bc4b"),
                    ("4d.05", "f49dbc4b381e7096d5ffe3c16660dd63121dabf7")],
        "lookup": [("pr5317.76", "c7986aa9127ef5b50b845434cb4e81dff9861cd2")],
    },
    # hvymetal: 315-5135, sys1pio, ROT0. Main: 32K fixed + 4 banks of 16K
    # (MAME region 0x10000), palette from 3 color PROMs (R, G, B)
    "hvymetal": {
        "crypt": "315-5135",
        "rot0": True,
        "banked": True,
        # ~400 KB decoded: sprites (128K, read per sprite line), the 64K
        # banked ROM and the opcode image (CPU reads via the flash cache)
        # stay PLAIN; tiles (read per pixel row) fit the Arena
        "plain": ["sprites", "banks", "opcodes"],
        "main": [("epr-6790a.1", "63dde673bd875dd23d445b152decb1d70c3750a4"),
                 ("epr-6789a.2", "07ef58ee2a5212e1e2800efc2bd48d2b2a9ed10d"),
                 ("epr-6788a.3", "3236313d5d826873d58af5ad80652c8d0ae0cc31")],
        "sound": [("epr-6787.120", "2b16c2702d3230891b700714a66ece95f1a74b44")],
        "tiles": [("epr-6795.62", "9aabfad143748e2ec1b41fde72a1d533bac3f9d8"),
                  ("epr-6796.61", "cfa5370aa430947637bfe57a5a1f802f273b43f7"),
                  ("epr-6793.64", "9bb9fff24fe057fa17057ba9263d412905a0c036"),
                  ("epr-6794.63", "90a0cc81d917122c726238585eb802763d34884e"),
                  ("epr-6791.66", "d9bac10aa7ac591a20bfed4e391ec1669eadc32d"),
                  ("epr-6792.65", "1e61c6e14c3c736e74e6c2ff5cde71d1d20b99a4")],
        "sprites": [("epr-6778.117", "90879d4d1bef38714a39ca71c101bd103d250284"),
                    ("epr-6777.110", "34c12b7de22169d369ff5b8a8d86da62404267f8"),
                    ("epr-6780.4", "aa1ce0b1666e17db196bd1e079691fbe433a9226"),
                    ("epr-6779.5", "7e742c09e832d01f74fe4025d194cbc8d2f24b70")],
        "color": [("pr7036.3", "0a2ac871383b115c16491b9ba5973f0d363eac49"),
                  ("pr7035.2", "14c3a585c083dc387532d64bfd63e34f5220e6de"),
                  ("pr7034.1", "56cba261819fd5f2beab56ffd80bb3fd328efe3e")],
        "lookup": [("pr5317p.4", "c7986aa9127ef5b50b845434cb4e81dff9861cd2")],
    },
    # gardia: 317-0006 (segacrp2), sys1pio, ROT270, Heavy Metal layout: 32K
    # fixed + 4 banks of 16K, 128K sprites, palette from 3 color PROMs
    "gardia": {
        "crypt": "317-0006",
        "banked": True,
        # sprites (128K) and the opcode image stay PLAIN; the 64K banks fit
        # the Arena (174 KB total) and pack to about half in flash
        "plain": ["sprites", "opcodes"],
        "main": [("epr-10255.1", "f19e345e5fae6a518276cc1bd09d1e2083672b25"),
                 ("epr-10254.2", "de1faf33cca031b72052bf5244fcb0bd79d85659"),
                 ("epr-10253.3", "44196f0a6c4c2b22a68609ddfc75be6a7877a69a")],
        "sound": [("epr-10243.120", "3f2bfc03e0f1053a4aa0ec5ebb0d573f2e20964c")],
        "tiles": [("epr-10249.61", "b76c155b674f3ad8938278d5dbb0452351c716a5"),
                  ("epr-10248.64", "39b28a103d8bfe702a376ebd880d6060e3d1ab30"),
                  ("epr-10247.66", "6660204c74a9f7e63b3ba08d99fb854aa863710e")],
        "sprites": [("epr-10234.117", "044836885ace8294124b1be9b3a4828f772bb9ee"),
                    ("epr-10233.110", "b37d7f261be12616dbe11dfa375eaf6878e4a0f3"),
                    ("epr-10236.04", "616f6097afddffa9af89fe84d8b6df59c567c1e6"),
                    ("epr-10235.5", "a575f9d5c026e6b18e990720ec7520b6b5ae94e3")],
        "color": [("pr-7345.3", "b5694c120f604a5f7cc95618a71ab16a1a6151ed"),
                  ("pr-7344.2", "d4f8790db4dce75e27156a4c6de2dcef2baf6d76"),
                  ("pr-7343.1", "ac37458d1feb6566b09a795b20c21953d4ab109d")],
        "lookup": [("pr5317.4", "c7986aa9127ef5b50b845434cb4e81dff9861cd2")],
    },
}

# MAME segacrpt_device.cpp sega_315_5048_device::decrypt(): per row
# (A0, A4, A8, A12) an opcode and a data entry, indexed by src bits 3, 5
CRYPT = {
    "315-5048": [
        (0x88, 0x08, 0x80, 0x00), (0xa0, 0xa8, 0x80, 0x88),
        (0x88, 0x08, 0x80, 0x00), (0x88, 0xa8, 0x80, 0xa0),
        (0xa0, 0x80, 0x20, 0x00), (0x88, 0x08, 0x80, 0x00),
        (0xa0, 0xa8, 0x80, 0x88), (0x88, 0x08, 0x80, 0x00),
        (0x28, 0x20, 0xa8, 0xa0), (0xa0, 0xa8, 0x80, 0x88),
        (0x88, 0xa8, 0x80, 0xa0), (0x28, 0x20, 0xa8, 0xa0),
        (0xa0, 0x80, 0x20, 0x00), (0xa0, 0xa8, 0x80, 0x88),
        (0x28, 0x20, 0xa8, 0xa0), (0xa0, 0xa8, 0x80, 0x88),
        (0xa0, 0x80, 0x20, 0x00), (0xa0, 0x80, 0x20, 0x00),
        (0xa0, 0x20, 0x80, 0x00), (0x88, 0xa8, 0x80, 0xa0),
        (0xa0, 0x20, 0x80, 0x00), (0xa0, 0x20, 0x80, 0x00),
        (0xa0, 0x20, 0x80, 0x00), (0xa0, 0x20, 0x80, 0x00),
        (0xa0, 0x80, 0x20, 0x00), (0xa0, 0x80, 0x20, 0x00),
        (0x88, 0xa8, 0x80, 0xa0), (0x28, 0x20, 0xa8, 0xa0),
        (0xa0, 0xa8, 0x80, 0x88), (0xa0, 0x80, 0x20, 0x00),
        (0x28, 0x20, 0xa8, 0xa0), (0xa0, 0xa8, 0x80, 0x88),
    ],
    "315-5041": [
        (0x28, 0xa8, 0x08, 0x88), (0x88, 0x80, 0x08, 0x00),
        (0x88, 0x08, 0x80, 0x00), (0x88, 0x80, 0x08, 0x00),
        (0x28, 0x08, 0xa8, 0x88), (0x28, 0xa8, 0x08, 0x88),
        (0x88, 0x08, 0x80, 0x00), (0x88, 0x08, 0x80, 0x00),
        (0x28, 0x08, 0xa8, 0x88), (0x88, 0x80, 0x08, 0x00),
        (0x88, 0x80, 0x08, 0x00), (0x28, 0xa8, 0x08, 0x88),
        (0xa0, 0x80, 0xa8, 0x88), (0x28, 0x08, 0xa8, 0x88),
        (0xa0, 0x80, 0xa8, 0x88), (0xa0, 0x80, 0xa8, 0x88),
        (0x88, 0x80, 0x08, 0x00), (0x88, 0x80, 0x08, 0x00),
        (0x88, 0x08, 0x80, 0x00), (0x88, 0x80, 0x08, 0x00),
        (0xa0, 0x80, 0x20, 0x00), (0x28, 0x08, 0xa8, 0x88),
        (0xa0, 0x80, 0x20, 0x00), (0x88, 0x08, 0x80, 0x00),
        (0x28, 0x08, 0xa8, 0x88), (0xa0, 0x80, 0x20, 0x00),
        (0xa0, 0x80, 0x20, 0x00), (0xa0, 0x80, 0x20, 0x00),
        (0xa0, 0x80, 0xa8, 0x88), (0x28, 0x08, 0xa8, 0x88),
        (0xa0, 0x80, 0x20, 0x00), (0xa0, 0x80, 0xa8, 0x88),
    ],
    "315-5110": [
        (0x88, 0x08, 0x80, 0x00), (0x00, 0x20, 0x80, 0xa0),
        (0x00, 0x20, 0x80, 0xa0), (0x88, 0x08, 0x80, 0x00),
        (0x88, 0x08, 0xa8, 0x28), (0x00, 0x20, 0x80, 0xa0),
        (0x00, 0x20, 0x80, 0xa0), (0x88, 0x08, 0xa8, 0x28),
        (0x00, 0x20, 0x80, 0xa0), (0x08, 0x00, 0x88, 0x80),
        (0x00, 0x20, 0x80, 0xa0), (0x20, 0x28, 0xa0, 0xa8),
        (0x20, 0x28, 0xa0, 0xa8), (0x00, 0x20, 0x80, 0xa0),
        (0x20, 0x28, 0xa0, 0xa8), (0x88, 0x08, 0xa8, 0x28),
        (0x88, 0x08, 0x80, 0x00), (0x08, 0x00, 0x88, 0x80),
        (0x08, 0x00, 0x88, 0x80), (0x88, 0x08, 0x80, 0x00),
        (0x08, 0x28, 0x00, 0x20), (0x08, 0x28, 0x00, 0x20),
        (0x88, 0x08, 0x80, 0x00), (0x08, 0x28, 0x00, 0x20),
        (0x08, 0x28, 0x00, 0x20), (0x08, 0x00, 0x88, 0x80),
        (0x08, 0x28, 0x00, 0x20), (0x20, 0x28, 0xa0, 0xa8),
        (0x20, 0x28, 0xa0, 0xa8), (0x08, 0x28, 0x00, 0x20),
        (0x20, 0x28, 0xa0, 0xa8), (0x08, 0x28, 0x00, 0x20),
    ],
    "315-5135": [
        (0x88, 0xa8, 0x80, 0xa0), (0xa0, 0x80, 0xa8, 0x88),
        (0x88, 0xa8, 0x80, 0xa0), (0x88, 0x80, 0x08, 0x00),
        (0xa0, 0x80, 0xa8, 0x88), (0x88, 0xa8, 0x80, 0xa0),
        (0x88, 0xa8, 0x80, 0xa0), (0x88, 0x80, 0x08, 0x00),
        (0xa0, 0x80, 0xa8, 0x88), (0x88, 0x80, 0x08, 0x00),
        (0x88, 0x80, 0x08, 0x00), (0x88, 0x80, 0x08, 0x00),
        (0xa0, 0x80, 0xa8, 0x88), (0x88, 0x80, 0x08, 0x00),
        (0x88, 0x80, 0x08, 0x00), (0x28, 0x08, 0xa8, 0x88),
        (0xa0, 0x20, 0xa8, 0x28), (0x88, 0xa8, 0x80, 0xa0),
        (0xa0, 0x20, 0xa8, 0x28), (0x88, 0xa8, 0x80, 0xa0),
        (0xa0, 0x20, 0xa8, 0x28), (0x88, 0xa8, 0x80, 0xa0),
        (0x88, 0xa8, 0x80, 0xa0), (0x28, 0x08, 0xa8, 0x88),
        (0x28, 0xa8, 0x08, 0x88), (0xa0, 0x20, 0xa8, 0x28),
        (0xa0, 0x20, 0xa8, 0x28), (0x28, 0xa8, 0x08, 0x88),
        (0x28, 0xa8, 0x08, 0x88), (0xa0, 0x20, 0xa8, 0x28),
        (0x28, 0x08, 0xa8, 0x88), (0x28, 0xa8, 0x08, 0x88),
    ],
}

# MAME segacrp2_device.cpp decode(): per row (address bits 14, 12, 9, 6, 3, 0)
# an opcode and a data entry, each a bit swap of bits 6/4/2/0 plus an XOR
SWAPS2 = [
    (6, 4, 2, 0), (4, 6, 2, 0), (2, 4, 6, 0), (0, 4, 2, 6), (6, 2, 4, 0), (6, 0, 2, 4),
    (6, 4, 0, 2), (2, 6, 4, 0), (4, 2, 6, 0), (4, 6, 0, 2), (6, 0, 4, 2), (0, 6, 4, 2),
    (4, 0, 6, 2), (0, 4, 6, 2), (6, 2, 0, 4), (2, 6, 0, 4), (0, 6, 2, 4), (2, 0, 6, 4),
    (0, 2, 6, 4), (4, 2, 0, 6), (2, 4, 0, 6), (4, 0, 2, 6), (2, 0, 4, 6), (0, 2, 4, 6),
]
# (xor_table[128], swap_table[128]); 315-5162 repeats a 16 entry XOR cycle and
# steps the swap index once per 16 entries, starting one entry early
CRYPT2 = {
    "315-5162": (
        [0x40, 0x10, 0x50, 0x04, 0x44, 0x14, 0x54, 0x01,
         0x41, 0x11, 0x51, 0x05, 0x45, 0x15, 0x55, 0x00] * 8,
        [4 + (i + 1) // 16 for i in range(128)],
    ),
    # sega_decode_317(): one 131 entry table pair, 317-0006 starts at entry 2
    "317-0006": (
        [0x04, 0x54, 0x44, 0x14, 0x15, 0x15, 0x51, 0x41, 0x41, 0x14, 0x10, 0x50, 0x15, 0x55, 0x54, 0x05,
         0x04, 0x41, 0x51, 0x01, 0x05, 0x10, 0x55, 0x51, 0x05, 0x05, 0x54, 0x11, 0x45, 0x05, 0x04, 0x14,
         0x10, 0x55, 0x01, 0x41, 0x51, 0x05, 0x55, 0x04, 0x45, 0x41, 0x55, 0x14, 0x45, 0x10, 0x04, 0x45,
         0x55, 0x50, 0x40, 0x00, 0x11, 0x45, 0x15, 0x00, 0x01, 0x00, 0x40, 0x00, 0x01, 0x45, 0x11, 0x00,
         0x45, 0x00, 0x44, 0x54, 0x40, 0x04, 0x05, 0x15, 0x15, 0x10, 0x15, 0x04, 0x01, 0x05, 0x50, 0x11,
         0x00, 0x44, 0x44, 0x04, 0x04, 0x01, 0x50, 0x05, 0x51, 0x00, 0x45, 0x44, 0x50, 0x15, 0x54, 0x40,
         0x41, 0x45, 0x40, 0x10, 0x14, 0x15, 0x40, 0x51, 0x50, 0x50, 0x45, 0x00, 0x10, 0x15, 0x05, 0x51,
         0x50, 0x44, 0x01, 0x15, 0x40, 0x04, 0x01, 0x44, 0x50, 0x44, 0x50, 0x50, 0x50, 0x10, 0x44, 0x04,
         0x40, 0x04, 0x10][2:130],
        [7, 7, 12, 1, 18, 11, 8, 23, 21, 17, 0, 23, 22, 0, 21, 15,
         13, 19, 21, 20, 20, 12, 13, 10, 20, 0, 14, 18, 6, 18, 3, 5,
         5, 20, 20, 13, 8, 0, 20, 18, 4, 14, 8, 5, 17, 6, 22, 10,
         0, 21, 0, 1, 6, 11, 17, 9, 17, 3, 9, 21, 0, 4, 16, 1,
         13, 17, 21, 5, 3, 7, 2, 16, 18, 13, 6, 19, 11, 23, 3, 20,
         3, 2, 18, 10, 18, 23, 19, 23, 3, 15, 0, 10, 5, 12, 0, 0,
         11, 22, 8, 14, 8, 6, 1, 15, 7, 11, 2, 17, 10, 15, 8, 21,
         10, 0, 2, 6, 1, 1, 3, 1, 12, 18, 16, 5, 0, 15, 17, 15,
         10, 20, 1][2:130],
    ),
}
CRYPT_SIZE = 0x8000
CRYPT_BITS = 0xa8  # bits 3, 5, 7 are swapped/inverted, the rest pass through


# files: (name, sha1) from zip_path, or (zip, name, sha1) from romszip/<zip>
def load(zip_path, files):
    out = b""
    for f in files:
        path = zip_path if len(f) == 2 else os.path.join(os.path.dirname(zip_path), f[0])
        name, sha1 = f[-2:]
        out += bytes(load_file(path, [name], sha1))
    return out


def bit(v, n):
    return (v >> n) & 1


# MAME segacrpt decode(): data image of the whole ROM, opcode image of 0000-7fff
def sega_decrypt(rom, table):
    data = bytearray(rom)
    ops = bytearray(CRYPT_SIZE)
    for a in range(CRYPT_SIZE):
        src = rom[a]
        row = bit(a, 0) | bit(a, 4) << 1 | bit(a, 8) << 2 | bit(a, 12) << 3
        col = bit(src, 3) | bit(src, 5) << 1
        xor = 0
        if src & 0x80:
            col = 3 - col
            xor = CRYPT_BITS
        keep = src & ~CRYPT_BITS
        ops[a] = keep | (table[2 * row][col] ^ xor)
        data[a] = keep | (table[2 * row + 1][col] ^ xor)
    return bytes(data), bytes(ops)


def swap2(src, t):
    out = src & 0xaa
    for dst, s in zip((6, 4, 2, 0), t):
        out |= bit(src, s) << dst
    return out


# MAME segacrp2 decode(): data image of the whole ROM, opcode image of 0000-7fff
def sega_decrypt2(rom, tables):
    xor, swap = tables
    data = bytearray(rom)
    ops = bytearray(CRYPT_SIZE)
    for a in range(CRYPT_SIZE):
        src = rom[a]
        row = (bit(a, 14) << 5 | bit(a, 12) << 4 | bit(a, 9) << 3 |
               bit(a, 6) << 2 | bit(a, 3) << 1 | bit(a, 0))
        ops[a] = swap2(src, SWAPS2[swap[2 * row]]) ^ xor[2 * row]
        data[a] = swap2(src, SWAPS2[swap[2 * row + 1]]) ^ xor[2 * row + 1]
    return bytes(data), bytes(ops)


# (data image, opcode image or None) per the game's "crypt" key
def decrypt_main(spec, rom):
    name = spec.get("crypt")
    if name in CRYPT:
        return sega_decrypt(rom, CRYPT[name])
    if name in CRYPT2:
        return sega_decrypt2(rom, CRYPT2[name])
    assert name is None, f"unknown crypt {name}"
    return rom, None


# charlayout: 3 planes RGN_FRAC(n,3), plane 0 = MSB, byte bit 7 = left pixel.
# Emitted column-major: word per native pixel column, native row 0 in bits 23..21.
# ROT0 games (transpose) render along native lines: word per native pixel row,
# native column 0 in bits 23..21.
def decode_tiles(rom, transpose=False):
    plane = len(rom) // 3
    count = plane // 8
    assert count & (count - 1) == 0, "tile count must be a power of two (code mask)"
    out = []
    for c in range(count):
        for outer in range(8):
            w = 0
            for inner in range(8):
                row, col = (outer, inner) if transpose else (inner, outer)
                px = 0
                for p in range(3):
                    px = (px << 1) | bit(rom[p * plane + c * 8 + row], 7 - col)
                w = (w << 3) | px
            out.append(w)
    return out, count


# Unique tiles in first-use order plus code -> unique index map (Arena budget)
def dedupe_tiles(words):
    index = {}
    unique = []
    tile_map = []
    for c in range(len(words) // 8):
        key = tuple(words[c * 8:c * 8 + 8])
        if key not in index:
            index[key] = len(index)
            unique.extend(key)
        tile_map.append(index[key])
    return unique, len(index), tile_map


def palette565():
    rg, b = [995, 495, 250], [495, 250]
    (wr, wg, wb), _ = resistor_weights(0, 255, -1.0, [(rg, 0, 0), (rg, 0, 0), (b, 0, 0)])
    return [rgb888_to_rgb565_le(combine(wr, bit(i, 0), bit(i, 1), bit(i, 2)),
                                combine(wg, bit(i, 3), bit(i, 4), bit(i, 5)),
                                combine(wb, bit(i, 6), bit(i, 7))) for i in range(256)]


# MAME system1_palette() with color PROMs (R, G, B, 4 bits each)
def prom_palette565(prom):
    def level(v):
        return 0x0e * bit(v, 0) + 0x1f * bit(v, 1) + 0x43 * bit(v, 2) + 0x8f * bit(v, 3)
    return [rgb888_to_rgb565_le(level(prom[i]), level(prom[256 + i]), level(prom[512 + i]))
            for i in range(256)]


def write_game_palette(game, vals):
    path = os.path.join(MACHINES, game, f"{game}_palette.h")
    with open(path, "w", newline="\n") as f:
        f.write("// Generated by romconv/internal/system1/system1_rom_convert.py; do not edit.\n")
        f.write("// Palette RAM byte -> RGB565 byte-swapped, from the color PROMs.\n")
        f.write(f"static const unsigned short {game}_palette[256] = {{\n")
        for i in range(0, 256, 8):
            f.write("  " + ", ".join(f"0x{v:04X}" for v in vals[i:i + 8]) + ",\n")
        f.write("};\n")


def write_palette():
    path = os.path.join(MACHINES, "system1", "system1_palette.h")
    vals = palette565()
    with open(path, "w", newline="\n") as f:
        f.write("// Generated by romconv/internal/system1/system1_rom_convert.py; do not edit.\n")
        f.write("// System 1 palette RAM byte (BBGGGRRR) -> RGB565 byte-swapped.\n")
        f.write("static const unsigned short system1_palette[256] = {\n")
        for i in range(0, 256, 8):
            f.write("  " + ", ".join(f"0x{v:04X}" for v in vals[i:i + 8]) + ",\n")
        f.write("};\n")


def plain_body(dims, values):
    if not dims:
        return ",\n".join(", ".join(f"0x{v:02x}" for v in values[i:i + 16])
                          for i in range(0, len(values), 16))
    n = int(dims[1:-1])
    return ",\n".join("{" + ", ".join(f"0x{v:08x}" for v in values[i:i + n]) + "}"
                      for i in range(0, len(values), n))


def emit(game, asset, ctype, dims, count, values, plain=False):
    path = os.path.join(MACHINES, game, f"{game}_{asset}.h")
    with open(path, "w", newline="\n") as f:
        if plain:
            emit_plain(f, f"{game}_{asset}", ctype, dims, count, plain_body(dims, values))
        else:
            emit_compressed(f, f"{game}_{asset}", ctype, dims, count, values)


# system1::system1() unpacks in this order; Arena::alloc is first fit, block A then B
UNPACK_ORDER = ["sprites", "tiles", "main_rom", "sound_rom", "lookup_prom", "tile_map", "opcodes",
                "banks"]
BANK_BASE = 0x8000  # "banked": main ROM image past 0x8000 is 16 KB banks at 8000-bfff


# assets of one game: (name, C type, dims, count, values, bytes)
def convert(game):
    spec = GAMES[game]
    zip_path = os.path.join(ROOT, "romszip", spec.get("zip", f"{game}.zip"))
    main_rom, opcodes = decrypt_main(spec, load(zip_path, spec["main"]))
    banks = None
    if spec.get("banked"):
        main_rom, banks = main_rom[:BANK_BASE], main_rom[BANK_BASE:]
    sound = load(zip_path, spec["sound"])
    tiles, tile_count = decode_tiles(load(zip_path, spec["tiles"]), spec.get("rot0", False))
    tiles, unique_count, tile_map = dedupe_tiles(tiles)
    sprites = load(zip_path, spec["sprites"])
    lookup = load(zip_path, spec["lookup"])
    assert len(sprites) % 0x8000 == 0, "sprite ROM is banked in 0x8000 units"
    # the mixer only collides under opaque sprite pixels (collision engine relies on it)
    assert all(lookup[i] & MIX_NO_COLLIDE for i in range(1, 128, 2))

    assets = [
        ("main_rom", "unsigned char", "", len(main_rom), main_rom, len(main_rom)),
        ("sound_rom", "unsigned char", "", len(sound), sound, len(sound)),
        ("tiles", "uint32_t", "[8]", unique_count, tiles, len(tiles) * 4),
        ("tile_map", "unsigned short", "", tile_count, tile_map, len(tile_map) * 2),
        ("sprites", "unsigned char", "", len(sprites), sprites, len(sprites)),
        ("lookup_prom", "unsigned char", "", len(lookup), lookup, len(lookup)),
    ]
    if opcodes:
        assets.append(("opcodes", "unsigned char", "", len(opcodes), opcodes, len(opcodes)))
    if banks:
        assets.append(("banks", "unsigned char", "", len(banks), banks, len(banks)))
    return assets


# simulates the ctor's unpack: (block A, block B) bytes used. PLAIN assets
# stay in flash.
def arena_blocks(assets, plain=()):
    size = {a[0]: a[5] for a in assets if a[0] not in plain}
    blocks = [0, 0]
    for name in UNPACK_ORDER:
        if name not in size:
            continue
        i = 0 if blocks[0] + size[name] <= ARENA_BLOCKS[0] else 1
        blocks[i] += size[name]
        assert blocks[i] <= ARENA_BLOCKS[i], f"{name} ({size[name]} B) fits no Arena block"
    return blocks


def main(game):
    spec = GAMES[game]
    plain = spec.get("plain", ())
    assets = convert(game)
    for name, ctype, dims, count, values, _ in assets:
        emit(game, name, ctype, dims, count, values, name in plain)
    write_palette()
    if "color" in spec:
        zip_path = os.path.join(ROOT, "romszip", spec.get("zip", f"{game}.zip"))
        write_game_palette(game, prom_palette565(load(zip_path, spec["color"])))

    a, b = arena_blocks(assets, plain)
    print(f"{game}: Arena {a + b} / {sum(ARENA_BLOCKS)} (blocks {a}, {b})")


if __name__ == "__main__":
    main(sys.argv[1])

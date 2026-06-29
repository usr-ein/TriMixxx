Your task is to research and document the Pioneer CDJ ProLink protocol which, over Ethernet, allows multiple CDJs to access each other's library.

I am building a unit which acts like a CDJ, called TriMiXxX, and uses the Mixxx opensource software on a Raspberry PI. I connect a usb drive to the TrimiXxX unit and Mixxx automounts it and can play its tracks, but I want to add the capability for Mixxx to:
1. See libraries from other CDJs on the same Ethernet network; and
2. Share its library with other CDJs on the network

To other CDJs, it should appear as a legitimate unit.

To accomplish this, I want to learn from the existing opensource litterature on the topic and make a PR of Mixxx that adds this capability.
For now though, it is sufficient for us to develop a Python program that just does the two above objectives, in order to formalise a way of doing it simply, and then once satisfied I will move to a real Mixxx integration.

Your task is to research the useful information for this project, called prolinks-compat, and gather it in a collection of markdown files under a `research` folder in this folder.

First though, write down the above mission in a CLAUDE.md file.

Here is a list of reference material you should start with after that:
https://github.com/evanpurkhiser/prolink-tools
https://github.com/evanpurkhiser/prolink-connect
https://github.com/grantHarris/prolink-cpp
https://github.com/teknopaul/libcdj
https://github.com/flesniak/python-prodj-link
https://github.com/Deep-Symmetry/dysentery
https://github.com/nzoschke/vizlink

Clone those in /research/ref-repos
Gitignore this folder of repos.

I own two CDJ-2000NXS units at home and therefore can test out the setup with my Mac and a python program, and an ethernet dongle. I can also gather new network frames etc if need be to research more.
Serval OpenWRT package feed
===========================
[Serval Project][], March 2014

This repository is an [OpenWRT package feed][] from the [Serval Project][].
Everybody is free to use this feed to include Serval's mesh networking into
their own OpenWRT project.

At present this feed only suports [OpenWRT 12.09][].

How to use this repository
--------------------------

When building [OpenWRT 12.09][], to put Serval packages into the list of
selectable packages, add the following line to the OpenWRT `feeds.conf` file:

    src-git serval git://github.com/servalproject/openwrt-packages.git;master

The name after the semicolon (such as `master` in the above example) is the
name of the *feed branch* within this repository.  You will need to choose the
feed branch most appropriate for your situation (see below).  If the branch
name is ommitted, the default branch *master* is used.

This repository **does not guarantee to provide stable Git SHA-1 references**
to any of its contents, because any branch may be [rewritten][] using [Git
rebase][] at any time.  The only references that are safe to use are the feed
branch names (see below).

OpenWRT packages
----------------

This feed supplies the following packages for [OpenWRT][]:

 * **[serval-dna][]** -- the [Serval DNA][] daemon, see the [Serval DNA OpenWRT
   build instructions][]

Feed branches
-------------

* **`master`**

  The default feed branch.  Contains all the most recently publicly released or
  publicly announced versions of Serval packages for OpenWRT.  This branch will
  generally only contain well tested or well supported versions of Serval
  packages, that will usually correspond to public releases of products, such
  as the [Serval Mesh][] app for Android (see below) or the [Serval Mesh
  Extender][] or the [Commotion Wireless OpenBTS][].

  Every change on this branch is announced publicly on the [Serval Project
  Developers][] mailing list.

* **`development`**

  The development feed branch.  Generally contains recent, unreleased, and
  potentially unstable (untested) Serval packages.  This branch may get updated
  often and without notice.  This feed branch is generally used by Serval's
  senior developers between releases to collaborate with developers in other
  organisations.

  Any questions or comments about this branch should be sent to the [Serval
  Project Developers][] mailing list.

* **`batphone-X.Y`** (eg, `batphone-0.91`, `batphone-1.0`)

  These feed branches contain stable versions of Serval packages that are
  compatible with release X.Y of the [Serval Mesh][] app for Android (a.k.a.
  “Batphone”).  These feed branches are always based off the *master* feed
  branch, and are normally created at the end of the [Serval Mesh release
  procedure][].

  The Batphone feed branches give the community a simple way to add Serval mesh
  networking to their OpenWRT devices which is known to be compatible with a
  given release of the [Serval Mesh][] app.

  The first commit in a Batphone feed branch is the branch's base on the
  *master* feed branch, which is normally the exact revision of [Serval DNA][]
  that was included in the [Serval Mesh][] release.  From that point on,
  though, the feed branch may advance independently:

  - if a change is made to [Serval DNA][] that only affects the [Batphone][]
    app, then a new [hotfix release][] of [Serval Mesh][] is made but there is
    no need to make a new OpenWRT release of the [serval-dna][] package,

  - if an exclusively OpenWRT issue is fixed in [Serval DNA][] (eg, portability
    bug, resource usage, configuration), then a new revision of the
    [serval-dna][] package can be released on all *batphone-X.Y* feed branches
    with which it is fully protocol compatible.

OpenWRT release procedure
-------------------------

The following examples all assume that:

 * `$HOME/src/serval-dna` is a clone of the [Serval DNA][] repository

 * `$HOME/src/openwrt-packages` is a clone of this [Serval OpenWRT package
   feed](./) repository

 * the environment has been set up with:

        export SERVAL_OPENWRT_PACKAGES_REPO="$HOME/src/openwrt-packages"

   (see the [sp-openwrt-release documentation][sp-openwrt-release])

 * [Serval Tools][serval-tools] have been installed, to provide the
   [sp-openwrt-release][] utility

Case 1 - New development version of Serval DNA
----------------------------------------------

Once a revision of [Serval DNA][] has been shown to successfully compile and
test on the OpenWRT platform, then it may be publicly released so that third
parties may confidently use it in their own development efforts.

The following example releases the HEAD of a fictitious *openwrt* [topic
branch][] of the [Serval DNA repository][] to the *development* feed branch,
then commits and pushes the change in a single operation:

    $ sp-openwrt-release --commit --push development ~/src/serval-dna=openwrt
    ...
    $

Henceforward, that revision will be the one downloaded and compiled by everyone
who has the following line in their OpenWRT `feeds.conf` file:

    src-git serval git://github.com/servalproject/openwrt-packages.git;development

Case 2 - New release of Serval DNA
----------------------------------

Whenever [Serval DNA][] is publicly released, typically as part of a Serval
product, the *master* feed branch is updated.

It is important to understand that every time [Serval DNA][] is formally
released to the public as part of a product, a new [product release
branch][Serval DNA development] is created in the [Serval DNA repository][] to
identify the exact version of the source code that was released.

The following example releases the HEAD of the (hypothetical) product release
branch called *meshextender-release-0.5* to the *master* feed, and commits and
pushes the change to GitHub in a single operation:

    $ sp-openwrt-release --commit --push master ~/src/serval-dna=meshextender-release-0.5
    ...
    $

Henceforward, that revision will be the one downloaded and compiled by everyone
who has any of the following lines in their OpenWRT `feeds.conf` file:

    src-git serval git://github.com/servalproject/openwrt-packages.git

or

    src-git serval git://github.com/servalproject/openwrt-packages.git;master

***So many branches!***  It can be confusing to work out what all these Git
branches are for and what they all mean. In a nutshell:

  * *feed branches* are in this repository, and are only relevant to OpenWRT.
    They are basically a mechanism for Serval to provide more than one feed
    of OpenWRT packages using a single Git repository;

  * source code branches like “product branches”, “topic branches”, etc. are in
    the [Serval DNA repository][], and are used by developers to organise their
    work and maintain more than one version of the software at a time.

Case 3 - New release of Serval Mesh (Batphone)
----------------------------------------------

In this example, a new version, **X.Y**, of the [Serval Mesh][] app for Android
(a.k.a. “Batphone”) is released.  This case is really a specialisation of Case
2 above, specifically for the [Batphone][] product.

It is important to understand that every time a new version of Batphone is
released, the [Serval Mesh release procedure][] creates a new *product release
branch* in the [Serval DNA repository][], named *batphone-release-X.Y*.  That
source-code branch identifies the version of Serval DNA that was contained in
the released [Serval Mesh][] app.  The HEAD of that branch that must now be
released for OpenWRT, as follows:

 1. The *master* feed's [serval-dna][] package is updated:

        $ sp-openwrt-release --commit --push master ~/src/serval-dna=batphone-release-X.Y
        ...
        $

 2. A new feed branch called *batphone-X.Y* is created, based off the *master*
    feed branch that was just updated in step 1:

        $ cd ~/src/openwrt-packages
        $ git checkout master
        Switched to branch 'master'
        Your branch is up-to-date with 'origin/master'.
        $ git branch batphone-X.Y
        $ git push origin batphone-X.Y
        Total 0 (delta 0), reused 0 (delta 0)
        To git@github.com:servalproject/openwrt-packages.git
         * [new branch]      batphone-X.Y -> batphone-X.Y
        $

Henceforward, everyone who has the following line in their OpenWRT `feeds.conf`
file will get a version of [Serval DNA][] which is fully compatible with
release X.Y of the [Serval Mesh][] app for Android:

    src-git serval git://github.com/servalproject/openwrt-packages.git;batphone-X.Y

More information
----------------

 * the [Serval DNA build instructions for OpenWRT][] give an introduction to
   the [OpenWRT build system][] and contain instructions for making OpenWRT
   releases of [Serval DNA][]

 * [Serval Tools README][serval-tools] has instructions for installing the
   [sp-openwrt-release][] utility

 * [sp-openwrt-release documentation][sp-openwrt-release] has more instructions
   for using the [sp-openwrt-release][] utility

 * the [sp-openwrt-release][] script has some built-in help:

        $ sp-openwrt-release --help

 * the [Serval Mesh release procedure][] describes how the [Serval Mesh][] app
   for Android ([Batphone][]) is released, and references this README


[Serval Project]: http://www.servalproject.org/
[OpenWRT]: https://www.openwrt.org/
[OpenWRT 12.09]: https://dev.openwrt.org/browser/tags/attitude_adjustment_12.09
[OpenWRT build system]: http://wiki.openwrt.org/about/toolchain
[OpenWRT package feed]: http://wiki.openwrt.org/doc/devel/feeds
[OpenWRT packages]: http://wiki.openwrt.org/doc/devel/packages
[serval-dna]: ./net/serval-dna/Makefile
[Serval Mesh]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:servalmesh:
[Serval DNA repository]: https://github.com/servalproject/serval-dna
[Batphone]: https://github.com/servalproject/batphone
[Serval DNA]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:servaldna:
[Serval DNA development]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:servaldna:development
[Serval DNA OpenWRT build instructions]: https://github.com/servalproject/serval-dna/blob/development/doc/OpenWRT.md
[Serval Mesh Extender]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:meshextender:
[Commotion Wireless OpenBTS]:http://www.commotionwireless.net/about/
[Serval Project Developers]: https://groups.google.com/d/forum/serval-project-developers
[Serval Mesh release procedure]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:servalmesh:release:
[hotfix release]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:servalmesh:release:#hotfix_release
[Serval DNA build instructions for OpenWRT]: https://github.com/servalproject/serval-dna/blob/development/doc/OpenWRT.md
[mesh networking]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:tech:mesh_network
[serval-tools]: https://github.com/servalproject/serval-tools
[sp-openwrt-release]: https://github.com/servalproject/serval-tools/blob/master/doc/sp-openwrt-release.md
[topic branch]: http://git-scm.com/book/en/Git-Branching-Branching-Workflows
[Git rebase]: http://git-scm.com/book/en/Git-Branching-Rebasing
[rewritten]: http://git-scm.com/book/en/Git-Tools-Rewriting-History

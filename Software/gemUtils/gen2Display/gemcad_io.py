# -*- coding: utf-8 -*-

def parseFacetLine(line, wheelIndex=96, appendTo=None):
    def facetParse(lineList, wheelInd=wheelIndex):
        facetName = False
        facetIndex = []

        for i, sL in enumerate(lineList):
            if facetName:
                if facetIndex:
                    facetIndex[-1]["name"] = sL
                facetName = False
                continue

            if sL == "n":
                facetName = True
                continue

            if sL == "":
                continue

            if sL == "G":
                facetComment = " ".join(lineList[(i + 1):]).rstrip()
                return facetIndex, facetComment

            try:
                idx = float(sL)
            except ValueError:
                continue

            facetIndex.append({
                "value": idx,
                "name": "",
                "deg": 360.0 * (idx / abs(wheelInd)),
                "frac": idx / abs(wheelInd)
            })

        return facetIndex, ""

    if appendTo is not None:
        facetIndex, facetComment = facetParse(
            line.split(" "),
            wheelInd=wheelIndex
        )
        appendTo["facets"] = appendTo["facets"] + facetIndex
        appendTo["comments"] = appendTo["comments"] + facetComment
        return appendTo

    if not line.startswith("a"):
        raise ValueError("Not a facet line!")

    splitLine = line.split(" ")

    angle = float(splitLine[1])
    trueForCrown = angle >= 0.0
    depth = float(splitLine[2])

    facetIndex, facetComment = facetParse(
        splitLine[3:],
        wheelInd=wheelIndex
    )

    facetDict = {
        "angle": angle,
        "isCrown": trueForCrown,
        "depth": depth,
        "facets": facetIndex,
        "nFacets": len(facetIndex),
        "comments": facetComment
    }

    return facetDict


def mergeSplitLines(txt):
    """
    In GemCAD spec, any line beginning with a space character is a continuation
    of the previous line.
    """
    txt = list(txt)

    while any(t.startswith(" ") for t in txt):
        whichLinesAreSplits = [
            i for i, t in enumerate(txt)
            if t.startswith(" ")
        ]

        for k in whichLinesAreSplits[::-1]:
            newLine = txt[k - 1].rstrip() + txt[k]
            txt[k - 1] = newLine
            del txt[k]

    return txt


def parseAllLines(txt):
    facetList = []

    doFirstLine = True
    doFirstTitle = True

    commentLines = []
    littleTitleLines = []

    gemCADver = None
    wheelIndex = 96
    meridian = 0.0
    foldSymmetry = None
    hasMirrorPlane = None
    refIndex = None
    bigName = ""

    for t in txt:
        t = t.rstrip("\n")

        if not t.strip():
            continue

        if doFirstLine:
            parts = t.split()
            try:
                gemCADver = float(parts[-1])
                doFirstLine = False
                continue
            except (ValueError, IndexError):
                doFirstLine = False

        if t.startswith("g"):
            p = t.split()
            wheelIndex = int(p[1])
            if len(p) > 2:
                try:
                    meridian = float(p[2])
                except ValueError:
                    meridian = 0.0

        elif t.startswith("y"):
            p = t.split()
            foldSymmetry = int(p[1])
            hasMirrorPlane = (p[2] == "y")

        elif t.startswith("I"):
            refIndex = float(t.split()[1])

        elif t.startswith("H"):
            if doFirstTitle:
                bigName = t[2:].rstrip()
                doFirstTitle = False
            else:
                littleTitleLines.append(t[2:].rstrip())

        elif t.startswith("a"):
            facetList.append(
                parseFacetLine(
                    t,
                    wheelIndex=wheelIndex
                )
            )

        elif t.startswith("F"):
            commentLines.append(t[2:].rstrip())

        elif t.startswith(" "):
            continue

    gemDict = {
        "gemCad_version": gemCADver,
        "wheelIndex": wheelIndex,
        "meridian": meridian,
        "foldsymmetry": foldSymmetry,
        "mirrorPlane": hasMirrorPlane,
        "refractiveIndex": refIndex,
        "boldTitle": bigName,
        "littleTitle": littleTitleLines,
        "facetList": facetList,
        "comments": commentLines
    }

    return gemDict


def loadGemCADFile(pth):
    with open(pth, "r") as fID:
        txt = fID.readlines()

    txt = mergeSplitLines(txt)
    gemDict = parseAllLines(txt)

    return gemDict
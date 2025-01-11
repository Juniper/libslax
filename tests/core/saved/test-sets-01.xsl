<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" xmlns:xutil="http://xml.libslax.org/xutil" version="1.0" extension-element-prefixes="slax-ext xutil">
  <xsl:variable name="list-temp-1">
    <item>
      <xsl:value-of select="5"/>
    </item>
    <item>
      <xsl:value-of select="6"/>
    </item>
    <item>seven</item>
  </xsl:variable>
  <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="list" select="slax-ext:node-set($list-temp-1)"/>
  <xsl:template match="/">
    <top>
      <one>
        <common>
          <xsl:copy-of select="xutil:common(&quot;seven&quot;, $list/*)"/>
        </common>
        <difference>
          <xsl:copy-of select="xutil:difference(&quot;seven&quot;, $list/*)"/>
        </difference>
      </one>
      <two>
        <common>
          <xsl:copy-of select="xutil:common($list/*, 5, &quot;seven&quot;)"/>
        </common>
        <difference>
          <xsl:copy-of select="xutil:difference($list/*, 5, &quot;seven&quot;)"/>
        </difference>
      </two>
      <xsl:variable name="top" select="/data"/>
      <xsl:for-each select="/data/test">
        <results>
          <xsl:choose>
            <xsl:when test="fourth">
              <common>
                <xsl:copy-of select="xutil:common(first/*, second/*, third/*, fourth/*)"/>
              </common>
              <difference>
                <xsl:copy-of select="xutil:difference(first/*, second/*, third/*, fourth/*)"/>
              </difference>
            </xsl:when>
            <xsl:when test="third">
              <common>
                <xsl:copy-of select="xutil:common(first/*, second/*, third/*)"/>
              </common>
              <difference>
                <xsl:copy-of select="xutil:difference(first/*, second/*, third/*)"/>
              </difference>
            </xsl:when>
            <xsl:otherwise>
              <common>
                <xsl:copy-of select="xutil:common(first/*, second/*)"/>
              </common>
              <difference>
                <xsl:copy-of select="xutil:difference(first/*, second/*)"/>
              </difference>
            </xsl:otherwise>
          </xsl:choose>
        </results>
      </xsl:for-each>
      <more>
        <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="s1" select="slax-ext:node-set($top/test[1]/first/*)"/>
        <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="s2" select="slax-ext:node-set($top/test[1]/second/*)"/>
        <common>
          <xsl:copy-of select="xutil:common($s1, $s2)"/>
        </common>
        <difference>
          <xsl:copy-of select="xutil:difference($s1, $s2)"/>
        </difference>
      </more>
      <even-more>
        <xsl:variable name="s1">
          <xsl:call-template name="build-first"/>
        </xsl:variable>
        <xsl:variable name="s2">
          <xsl:call-template name="build-second"/>
        </xsl:variable>
        <common>
          <xsl:copy-of select="xutil:common($s1, $s2)"/>
        </common>
        <difference>
          <xsl:copy-of select="xutil:difference($s1, $s2)"/>
        </difference>
      </even-more>
    </top>
  </xsl:template>
  <xsl:template name="build-first">
    <only-in-first>
      <mumble>
        <jumble>
          <gumble>test</gumble>
        </jumble>
      </mumble>
      <frumble>
        <jumble>
          <gumble>test</gumble>
        </jumble>
      </frumble>
    </only-in-first>
    <not-the-same>
      <a>alpha</a>
      <b>bravo</b>
      <c>charlie</c>
    </not-the-same>
    <also-not-the-same>
      <a>alpha</a>
      <b>bravo</b>
    </also-not-the-same>
    <has-spaces>
      <lots-of-spaces>
        <here>yes</here>
      </lots-of-spaces>
      <lots-of-spaces>
        <here>too</here>
      </lots-of-spaces>
    </has-spaces>
    <in-both>
      <interface>
        <name>ge-1/1/1</name>
        <unit>
          <name>1</name>
          <vlan-id>1</vlan-id>
          <family>
            <inet>
              <address>
                <name>1.1.1.1/30</name>
              </address>
            </inet>
            <mpls/>
          </family>
        </unit>
      </interface>
    </in-both>
  </xsl:template>
  <xsl:template name="build-second">
    <not-in-first>
      <yes>sure</yes>
    </not-in-first>
    <in-both>
      <interface>
        <name>ge-1/1/1</name>
        <unit>
          <name>1</name>
          <vlan-id>1</vlan-id>
          <family>
            <inet>
              <address>
                <name>1.1.1.1/30</name>
              </address>
            </inet>
            <mpls/>
          </family>
        </unit>
      </interface>
    </in-both>
    <has-spaces>
      <lots-of-spaces>
        <here>yes</here>
      </lots-of-spaces>
      <lots-of-spaces>
        <here>too</here>
      </lots-of-spaces>
    </has-spaces>
    <not-the-same>
      <a>alpha</a>
      <b>bravo</b>
    </not-the-same>
    <also-not-the-same>
      <a>alpha</a>
      <b>bravo</b>
      <c>charlie</c>
    </also-not-the-same>
  </xsl:template>
</xsl:stylesheet>

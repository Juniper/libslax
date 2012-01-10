<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://xml.libslax.org/slax" xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" version="1.0" extension-element-prefixes="slax slax-ext">
  <xsl:output indent="yes"/>
  <xsl:template match="/">
    <out>
      <xsl:variable name="slax-a" mvarname="a"/>
      <xsl:variable name="a" select="&quot;a&quot;" mutable="yes" svarname="slax-a"/>
      <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="a" svarname="slax-a" select="&quot;b&quot;"/>
      <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="a" svarname="slax-a" select="&quot;c&quot;"/>
      <xsl:variable xmlns:xsl="http://www.w3.org/1999/XSL/Transform" name="slax-b" mvarname="b">
        <b>bee</b>
      </xsl:variable>
      <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="b" mutable="yes" select="slax:mvar-init(&quot;slax-b&quot;)" svarname="slax-b"/>
      <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="b" svarname="slax-b">
        <c>sea</c>
      </slax:append-to-variable>
      <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="b" svarname="slax-b">
        <d>dee</d>
      </slax:append-to-variable>
      <a>
        <xsl:copy-of select="$a"/>
      </a>
      <b>
        <xsl:copy-of select="$b"/>
      </b>
      <xsl:call-template name="main"/>
    </out>
  </xsl:template>
  <xsl:template name="main">
    <main>
      <xsl:variable xmlns:xsl="http://www.w3.org/1999/XSL/Transform" name="slax-x" mvarname="x">
        <xsl:call-template name="test4">
          <xsl:with-param name="line" select="1"/>
        </xsl:call-template>
        <xsl:call-template name="test4">
          <xsl:with-param name="line" select="5"/>
        </xsl:call-template>
      </xsl:variable>
      <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="x" mutable="yes" select="slax:mvar-init(&quot;slax-x&quot;)" svarname="slax-x"/>
      <xsl:variable xmlns:xsl="http://www.w3.org/1999/XSL/Transform" name="slax-y" mvarname="y">
        <xsl:call-template name="test4">
          <xsl:with-param name="line" select="10"/>
        </xsl:call-template>
        <xsl:call-template name="test4">
          <xsl:with-param name="line" select="15"/>
        </xsl:call-template>
      </xsl:variable>
      <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="y" mutable="yes" select="slax:mvar-init(&quot;slax-y&quot;)" svarname="slax-y"/>
      <slax:trace xmlns:slax="http://xml.libslax.org/slax" select="$x"/>
      <xsl:variable name="slax-x1" mvarname="x1"/>
      <xsl:variable name="x1" select="4" mutable="yes" svarname="slax-x1"/>
      <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="x1" svarname="slax-x1" select="5"/>
      <xsl:variable name="y1" select="10"/>
      <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="x1" svarname="slax-x1" select="$y"/>
      <x1>
        <xsl:copy-of select="$x1"/>
      </x1>
      <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="x" svarname="slax-x" select="$y"/>
      <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="y" svarname="slax-y" select="$x"/>
      <x>
        <xsl:copy-of select="$x"/>
      </x>
      <y>
        <xsl:copy-of select="$y"/>
      </y>
      <!-- 
	call test3();
	call test1();
	call test2();
 -->
    </main>
  </xsl:template>
  <xsl:template name="test4">
    <xsl:param name="line" select="0"/>
    <xsl:variable name="z">
      <error>
        <line>
          <xsl:value-of select="$line"/>
        </line>
        <message>fake</message>
      </error>
      <error>
        <line>
          <xsl:value-of select="$line + 1"/>
        </line>
        <message>fake</message>
      </error>
      <error>
        <line>
          <xsl:value-of select="$line + 2"/>
        </line>
        <message>fake</message>
      </error>
      <error>
        <line>
          <xsl:value-of select="$line + 3"/>
        </line>
        <message>fake</message>
      </error>
    </xsl:variable>
    <slax:trace xmlns:slax="http://xml.libslax.org/slax" select="$z"/>
    <xsl:copy-of select="$z"/>
  </xsl:template>
  <xsl:template name="test1">
    <xsl:variable name="slax-test" mvarname="test"/>
    <xsl:variable name="test" select="10" mutable="yes" svarname="slax-test"/>
    <test1>
      <xsl:value-of select="$test"/>
    </test1>
    <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="test" svarname="slax-test" select="20"/>
    <test1>
      <xsl:value-of select="$test"/>
    </test1>
    <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="test" svarname="slax-test" select="30"/>
    <test1>
      <xsl:value-of select="$test"/>
    </test1>
    <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="test" svarname="slax-test" select="40"/>
    <test1>
      <xsl:value-of select="$test"/>
    </test1>
  </xsl:template>
  <xsl:template name="test2">
    <xsl:variable name="slax-x" mvarname="x"/>
    <xsl:variable name="x" select="0" mutable="yes" svarname="slax-x"/>
    <xsl:variable name="slax-dot-1" select="."/>
    <xsl:for-each xmlns:slax="http://xml.libslax.org/slax" select="slax:build-sequence(1, 20)">
      <xsl:variable name="i" select="."/>
      <xsl:for-each select="$slax-dot-1">
        <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="x" svarname="slax-x" select="$x + $i"/>
        <test2>
          <xsl:value-of select="$x"/>
        </test2>
      </xsl:for-each>
    </xsl:for-each>
  </xsl:template>
  <xsl:template name="test3">
    <xsl:variable name="slax-errors" mvarname="errors"/>
    <xsl:variable name="errors" mutable="yes" svarname="slax-errors"/>
    <xsl:variable name="slax-dot-2" select="."/>
    <xsl:for-each xmlns:slax="http://xml.libslax.org/slax" select="slax:build-sequence(1, 20)">
      <xsl:variable name="i" select="."/>
      <xsl:for-each select="$slax-dot-2">
        <xsl:variable name="x-temp-1">
          <error>
            <line>
              <xsl:value-of select="$i"/>
            </line>
            <message>Shut 'er down Clancey!  She's a-pumping mud!</message>
          </error>
        </xsl:variable>
        <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="x" select="slax-ext:node-set($x-temp-1)"/>
        <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="errors" svarname="slax-errors" select="$x"/>
        <slax:trace xmlns:slax="http://xml.libslax.org/slax" select="$errors"/>
      </xsl:for-each>
    </xsl:for-each>
    <errors>
      <xsl:copy-of select="$errors"/>
    </errors>
  </xsl:template>
</xsl:stylesheet>

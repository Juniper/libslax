<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:output indent="yes"/>
  <xsl:template match="/">
    <out>
      <xsl:variable name="slax-a" mvarname="a"/>
      <xsl:variable name="a" mutable="yes" svarname="slax-a"/>
      <xsl:variable name="slax-b" mvarname="b"/>
      <xsl:variable name="b" select="42" mutable="yes" svarname="slax-b"/>
      <xsl:variable xmlns:xsl="http://www.w3.org/1999/XSL/Transform" name="slax-c" mvarname="c">
        <fish>blue</fish>
      </xsl:variable>
      <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="c" mutable="yes" select="slax:mvar-init(&quot;slax-c&quot;)" svarname="slax-c"/>
      <xsl:variable xmlns:xsl="http://www.w3.org/1999/XSL/Transform" name="slax-d" mvarname="d">
        <one>
          <xsl:value-of select="1"/>
        </one>
        <two>
          <xsl:value-of select="2"/>
        </two>
        <three>
          <xsl:value-of select="3"/>
        </three>
      </xsl:variable>
      <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="d" mutable="yes" select="slax:mvar-init(&quot;slax-d&quot;)" svarname="slax-d"/>
      <xsl:call-template name="print">
        <xsl:with-param name="name" select="&quot;mvar&quot;"/>
        <xsl:with-param name="a" select="$a"/>
        <xsl:with-param name="b" select="$b"/>
        <xsl:with-param name="c" select="$c"/>
        <xsl:with-param name="d" select="$d"/>
      </xsl:call-template>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="a" svarname="slax-a" select="&quot;happy&quot;"/>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="b" svarname="slax-b" select="99"/>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="c" svarname="slax-c">
        <fish>red</fish>
      </slax:set-variable>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="d" svarname="slax-d">
        <four>
          <xsl:value-of select="4"/>
        </four>
        <five>
          <xsl:value-of select="5"/>
        </five>
        <six>
          <xsl:value-of select="6"/>
        </six>
      </slax:set-variable>
      <xsl:call-template name="print">
        <xsl:with-param name="name" select="&quot;set&quot;"/>
        <xsl:with-param name="a" select="$a"/>
        <xsl:with-param name="b" select="$b"/>
        <xsl:with-param name="c" select="$c"/>
        <xsl:with-param name="d" select="$d"/>
      </xsl:call-template>
      <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="a" svarname="slax-a" select="&quot; camper&quot;"/>
      <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="b" svarname="slax-b" select="&quot; bottles&quot;"/>
      <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="c" svarname="slax-c">
        <fish>old</fish>
      </slax:append-to-variable>
      <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="d" svarname="slax-d">
        <seven>
          <xsl:value-of select="7"/>
        </seven>
        <eight>
          <xsl:value-of select="8"/>
        </eight>
        <nine>
          <xsl:value-of select="9"/>
        </nine>
      </slax:append-to-variable>
      <xsl:call-template name="print">
        <xsl:with-param name="name" select="&quot;append&quot;"/>
        <xsl:with-param name="a" select="$a"/>
        <xsl:with-param name="b" select="$b"/>
        <xsl:with-param name="c" select="$c"/>
        <xsl:with-param name="d" select="$d"/>
      </xsl:call-template>
    </out>
  </xsl:template>
  <xsl:template name="print">
    <xsl:param name="name"/>
    <xsl:param name="a"/>
    <xsl:param name="b"/>
    <xsl:param name="c"/>
    <xsl:param name="d"/>
    <xsl:element name="{$name}">
      <a>
        <xsl:copy-of select="$a"/>
      </a>
      <b>
        <xsl:copy-of select="$b"/>
      </b>
      <c>
        <xsl:copy-of select="$c"/>
      </c>
      <d>
        <xsl:copy-of select="$d"/>
      </d>
    </xsl:element>
  </xsl:template>
</xsl:stylesheet>

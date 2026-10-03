<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:output indent="yes"/>
  <xsl:template match="/">
    <out>
      <xsl:variable name="a" mutable="yes"/>
      <xsl:variable name="b" select="42" mutable="yes"/>
      <xsl:variable name="c" mutable="yes">
        <fish>blue</fish>
      </xsl:variable>
      <xsl:variable name="d" mutable="yes">
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
      <xsl:variable name="e" select="$d/two" mutable="yes"/>
      <xsl:variable name="f" select="$d/two"/>
      <xsl:call-template name="print">
        <xsl:with-param name="name" select="&quot;mvar&quot;"/>
        <xsl:with-param name="a" select="$a"/>
        <xsl:with-param name="b" select="$b"/>
        <xsl:with-param name="c" select="$c"/>
        <xsl:with-param name="d" select="$d"/>
      </xsl:call-template>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="a" select="&quot;happy&quot;"/>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="b" select="99"/>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="c">
        <fish>red</fish>
      </slax:set-variable>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="d">
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
      <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="a" select="&quot; camper&quot;"/>
      <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="b" select="&quot; bottles&quot;"/>
      <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="c">
        <fish>old</fish>
      </slax:append-to-variable>
      <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="d">
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

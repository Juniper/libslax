<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:my="mine" xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" xmlns:exsl="http://exslt.org/common" xmlns:slax-func="http://exslt.org/functions" version="1.0" extension-element-prefixes="slax-ext exsl slax-func">
  <!-- 
var $test = {
   "a": "a",
   "b": "b",
   "c": {
        "d": 1,
        "e": 2,
   }
}
 -->
  <xsl:template match="/">
    <top>
      <xsl:variable name="slax-temp-arg-1">
        <command>show something</command>
      </xsl:variable>
      <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="x0" select="my:test(0, slax-ext:node-set($slax-temp-arg-1), &quot;else&quot;)"/>
      <xsl:copy-of select="$x0"/>
      <xsl:variable name="slax-temp-arg-2">
        <first>one</first>
        <second>two</second>
        <xsl:if test="2 = 3">
          <bad>news</bad>
        </xsl:if>
        <xsl:if test="1 = 1">
          <good>news</good>
        </xsl:if>
      </xsl:variable>
      <xsl:variable name="slax-temp-arg-3">
        <third>three</third>
        <fourth>four</fourth>
      </xsl:variable>
      <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="x1" select="my:test(1, slax-ext:node-set($slax-temp-arg-2), slax-ext:node-set($slax-temp-arg-3))"/>
      <xsl:copy-of select="$x1"/>
      <xsl:variable name="slax-temp-arg-4">
        <command>foo</command>
      </xsl:variable>
      <xsl:call-template name="test">
        <xsl:with-param name="name" select="2"/>
        <xsl:with-param xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="a" select="slax-ext:node-set($slax-temp-arg-4)"/>
      </xsl:call-template>
      <xsl:variable name="slax-temp-arg-5">
        <too>to</too>
      </xsl:variable>
      <xsl:variable name="slax-temp-arg-6">
        <cc/>
      </xsl:variable>
      <xsl:call-template name="test">
        <xsl:with-param name="name" select="3"/>
        <xsl:with-param name="a" select="&quot;test&quot;"/>
        <xsl:with-param xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="b" select="slax-ext:node-set($slax-temp-arg-5)"/>
        <xsl:with-param xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="c" select="slax-ext:node-set($slax-temp-arg-6)"/>
      </xsl:call-template>
      <xsl:variable name="slax-temp-arg-7">
        <top>
          <one>
            <xsl:value-of select="1"/>
          </one>
          <two>
            <xsl:value-of select="2"/>
          </two>
          <three>
            <xsl:value-of select="3"/>
          </three>
        </top>
      </xsl:variable>
      <xsl:call-template name="test">
        <xsl:with-param name="name" select="4"/>
        <xsl:with-param xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="a" select="slax-ext:node-set($slax-temp-arg-7)"/>
        <xsl:with-param name="b" select="&quot;no problemo&quot;"/>
      </xsl:call-template>
      <xsl:variable name="slax-temp-arg-8">
        <x>x</x>
        <y>y</y>
      </xsl:variable>
      <xsl:call-template name="test">
        <xsl:with-param name="name" select="5"/>
        <xsl:with-param xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="a" select="slax-ext:node-set($slax-temp-arg-8)"/>
      </xsl:call-template>
      <xsl:variable name="slax-temp-arg-9">
        <x>x1</x>
        <y>y1</y>
      </xsl:variable>
      <xsl:variable name="slax-temp-arg-10">
        <z>z</z>
        <zz>zz</zz>
        <zzz>zzz</zzz>
      </xsl:variable>
      <xsl:call-template name="test">
        <xsl:with-param name="name" select="6"/>
        <xsl:with-param xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="a" select="slax-ext:node-set($slax-temp-arg-9)"/>
        <xsl:with-param xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="b" select="slax-ext:node-set($slax-temp-arg-10)"/>
      </xsl:call-template>
    </top>
  </xsl:template>
  <xsl:template name="test">
    <xsl:param name="name"/>
    <xsl:param name="a"/>
    <xsl:param name="b" select="/null"/>
    <template test="{$name}">
      <a type="{exsl:object-type($a)}">
        <xsl:copy-of select="$a"/>
      </a>
      <xsl:if test="$b">
        <b type="{exsl:object-type($b)}">
          <xsl:copy-of select="$b"/>
        </b>
      </xsl:if>
    </template>
  </xsl:template>
  <slax-func:function xmlns:slax-func="http://exslt.org/functions" name="my:test">
    <xsl:param name="name"/>
    <xsl:param name="a"/>
    <xsl:param name="b" select="/null"/>
    <slax-func:result xmlns:slax-func="http://exslt.org/functions">
      <function test="{$name}">
        <a type="{exsl:object-type($a)}">
          <xsl:copy-of select="$a"/>
        </a>
        <xsl:if test="$b">
          <b type="{exsl:object-type($b)}">
            <xsl:copy-of select="$b"/>
          </b>
        </xsl:if>
      </function>
    </slax-func:result>
  </slax-func:function>
</xsl:stylesheet>

<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:app="http://xml.juniper.net/wonder/app" xmlns:slax="http://xml.libslax.org/slax" xmlns:slax-func="http://exslt.org/functions" version="1.0" extension-element-prefixes="slax slax-func">
  <xsl:variable name="book" mutable="yes"/>
  <xsl:variable name="ref" mutable="yes"/>
  <xsl:template match="/">
    <top>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="book">
        <book>one</book>
        <book x="1">two</book>
        <book>three</book>
        <book y="1">four</book>
      </slax:set-variable>
      <xsl:for-each select="$book/*[@x]">
        <found-x>
          <xsl:value-of select="."/>
        </found-x>
      </xsl:for-each>
      <xsl:for-each select="$book/*[@y]">
        <found-y>
          <xsl:value-of select="."/>
        </found-y>
      </xsl:for-each>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="book">
        <book>five</book>
        <book x="1">six</book>
        <book>seven</book>
        <book y="1">eight</book>
      </slax:set-variable>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="ref" select="$book"/>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="ref" select="$book/*[@y]"/>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="ref" select="$book"/>
      <xsl:for-each select="$ref/*">
        <found-x>
          <xsl:value-of select="."/>
        </found-x>
      </xsl:for-each>
      <xsl:for-each select="$book/*[@y]">
        <found-y>
          <xsl:value-of select="."/>
        </found-y>
      </xsl:for-each>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="book" select="app:get()"/>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="ref" select="$book"/>
      <xsl:for-each select="$ref/*">
        <found-x>
          <xsl:value-of select="."/>
        </found-x>
      </xsl:for-each>
      <xsl:for-each select="$book/*[@y]">
        <found-y>
          <xsl:value-of select="."/>
        </found-y>
      </xsl:for-each>
      <book>
        <xsl:copy-of select="$book"/>
      </book>
    </top>
  </xsl:template>
  <slax-func:function xmlns:slax-func="http://exslt.org/functions" name="app:get">
    <slax-func:result xmlns:slax-func="http://exslt.org/functions">
      <book>nine</book>
      <book x="1">ten</book>
      <book>eleven</book>
      <book y="1">twelve</book>
    </slax-func:result>
  </slax-func:function>
</xsl:stylesheet>

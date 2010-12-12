<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://code.google.com/p/libslax/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:output indent="yes"/>
  <xsl:template match="/top">
    <out xmlns:xxx="http://code.google.com/p/libslax/slax">
      <xsl:variable name="x" select="5"/>
      <xsl:for-each xmlns:slax="http://code.google.com/p/libslax/slax" select="slax:build-sequence(1, 10)">
        <first>
          <xsl:value-of select="."/>
        </first>
      </xsl:for-each>
      <xsl:variable name="slax-dot-1" select="."/>
      <xsl:for-each xmlns:slax="http://code.google.com/p/libslax/slax" select="slax:build-sequence($x, count)">
        <xsl:variable name="i" select="."/>
        <xsl:for-each select="$slax-dot-1">
          <second count="{count}">
            <xsl:value-of select="$i"/>
          </second>
        </xsl:for-each>
      </xsl:for-each>
      <xsl:variable name="slax-dot-2" select="."/>
      <xsl:for-each xmlns:slax="http://code.google.com/p/libslax/slax" select="slax:build-sequence(1 - size, 10 div 3)">
        <xsl:variable name="i" select="."/>
        <xsl:for-each select="$slax-dot-2">
          <third>
            <xsl:value-of select="$i"/>
          </third>
        </xsl:for-each>
      </xsl:for-each>
      <!-- Simple test case -->
      <xsl:variable name="slax-dot-3" select="."/>
      <xsl:for-each select="item">
        <xsl:variable name="i" select="."/>
        <xsl:for-each select="$slax-dot-3">
          <li item="{class}">
            <xsl:value-of select="$i"/>
          </li>
        </xsl:for-each>
      </xsl:for-each>
      <!-- Test that 'sort' gets relocated -->
      <xsl:variable name="slax-dot-4" select="."/>
      <xsl:for-each select="item">
        <xsl:sort select="." order="ascending"/>
        <xsl:variable name="i" select="."/>
        <xsl:for-each select="$slax-dot-4">
          <ul item="{class}">
            <xsl:value-of select="$i"/>
          </ul>
        </xsl:for-each>
      </xsl:for-each>
      <!-- Test that this 'sort' is not relocated -->
      <xsl:for-each select="item">
        <xsl:sort select="." order="ascending"/>
        <ol item="{../class}">
          <xsl:value-of select="."/>
        </ol>
      </xsl:for-each>
    </out>
  </xsl:template>
</xsl:stylesheet>

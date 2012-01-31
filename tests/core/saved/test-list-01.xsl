<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:my="http://my.example.com" xmlns:slax="http://xml.libslax.org/slax" xmlns:slax-func="http://exslt.org/functions" version="1.0" extension-element-prefixes="slax slax-func">
  <xsl:variable name="key" select="1"/>
  <xsl:variable name="new">
    <slax:trace xmlns:slax="http://xml.libslax.org/slax">
      <xsl:choose>
        <xsl:when test="$key">
          <trace-test>testing...1...2...3...</trace-test>
        </xsl:when>
        <xsl:otherwise>
          <trace-test>testing...not...</trace-test>
        </xsl:otherwise>
      </xsl:choose>
    </slax:trace>
  </xsl:variable>
  <xsl:variable name="min" select="1"/>
  <xsl:variable name="max">
    <slax:trace xmlns:slax="http://xml.libslax.org/slax" select="concat(&quot;min: &quot;, $min)"/>
    <xsl:value-of select="10"/>
  </xsl:variable>
  <xsl:template match="/list">
    <top>
      <slax:trace xmlns:slax="http://xml.libslax.org/slax" select="my:test(10)"/>
      <slax:trace xmlns:slax="http://xml.libslax.org/slax" select="&quot;test&quot;"/>
      <slax:trace xmlns:slax="http://xml.libslax.org/slax" select="concat(&quot;count&quot;, count(.))"/>
      <slax:trace xmlns:slax="http://xml.libslax.org/slax" select="$max - $min"/>
      <slax:trace xmlns:slax="http://xml.libslax.org/slax">
        <top>
          <xsl:choose>
            <xsl:when test="$min = 0">
              <xsl:text>bad</xsl:text>
            </xsl:when>
            <xsl:otherwise>
              <xsl:text>good</xsl:text>
            </xsl:otherwise>
          </xsl:choose>
        </top>
      </slax:trace>
      <xsl:for-each select="item">
        <slax:trace xmlns:slax="http://xml.libslax.org/slax" select="name"/>
        <slax:trace xmlns:slax="http://xml.libslax.org/slax" select="concat(&quot;item: &quot;, name)"/>
        <found>
          <xsl:value-of select="name"/>
        </found>
      </xsl:for-each>
    </top>
  </xsl:template>
  <slax-func:function xmlns:slax-func="http://exslt.org/functions" name="my:test">
    <xsl:param name="value"/>
    <slax-func:result xmlns:slax-func="http://exslt.org/functions">
      <test>
        <xsl:value-of select="$value"/>
      </test>
    </slax-func:result>
  </slax-func:function>
</xsl:stylesheet>

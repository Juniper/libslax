<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:my="http://my.example.com" xmlns:slax-trace="http://code.google.com/p/libslax/trace" xmlns:slax-func="http://exslt.org/functions" version="1.0" extension-element-prefixes="slax-trace slax-func">
  <xsl:variable name="key" select="1"/>
  <xsl:variable name="new">
    <slax-trace:trace xmlns:slax-trace="http://code.google.com/p/libslax/trace">
      <xsl:choose>
        <xsl:when test="$key">
          <trace-test>testing...1...2...3...</trace-test>
        </xsl:when>
        <xsl:otherwise>
          <trace-test>testing...not...</trace-test>
        </xsl:otherwise>
      </xsl:choose>
    </slax-trace:trace>
  </xsl:variable>
  <xsl:variable name="min" select="1"/>
  <xsl:variable name="max">
    <slax-trace:trace xmlns:slax-trace="http://code.google.com/p/libslax/trace" select="concat(&quot;min: &quot;, $min)"/>
    <xsl:value-of select="10"/>
  </xsl:variable>
  <xsl:template match="/list">
    <top>
      <slax-trace:trace xmlns:slax-trace="http://code.google.com/p/libslax/trace" select="my:test(10)"/>
      <slax-trace:trace xmlns:slax-trace="http://code.google.com/p/libslax/trace" select="&quot;test&quot;"/>
      <slax-trace:trace xmlns:slax-trace="http://code.google.com/p/libslax/trace" select="concat(&quot;count&quot;, count(.))"/>
      <slax-trace:trace xmlns:slax-trace="http://code.google.com/p/libslax/trace" select="$max - $min"/>
      <slax-trace:trace xmlns:slax-trace="http://code.google.com/p/libslax/trace">
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
      </slax-trace:trace>
      <xsl:for-each select="item">
        <slax-trace:trace xmlns:slax-trace="http://code.google.com/p/libslax/trace" select="concat(&quot;item: &quot;, name)"/>
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

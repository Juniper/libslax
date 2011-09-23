<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://code.google.com/p/libslax/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:template match="/">
    <out>
      <xsl:variable name="test" select="&quot;&lt;test&gt;&lt;one&gt;1&lt;/one&gt;&lt;/test&gt;&quot;"/>
      <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="res" select="slax:string-to-xml($test)"/>
      <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="r2" select="slax:string-to-xml(&quot;&lt;a&gt;&quot;, &quot;&lt;b&gt;&quot;, &quot;&lt;c&gt;&quot;, $test, &quot;&lt;/c&gt;&quot;, &quot;&lt;/b&gt;&quot;, &quot;&lt;/a&gt;&quot;)"/>
      <one>
        <res>
          <xsl:copy-of select="$res"/>
        </res>
        <r2>
          <xsl:copy-of select="$r2"/>
        </r2>
      </one>
      <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="ores" select="slax:xml-to-string($res)"/>
      <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="or2" select="slax:xml-to-string($r2)"/>
      <two>
        <ores>
          <xsl:value-of select="$ores"/>
        </ores>
        <or2>
          <xsl:value-of select="$or2"/>
        </or2>
      </two>
      <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="xres" select="slax:string-to-xml($ores)"/>
      <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="xr2" select="slax:string-to-xml($or2)"/>
      <three>
        <xres>
          <xsl:copy-of select="$xres"/>
        </xres>
        <xr2>
          <xsl:copy-of select="$xr2"/>
        </xr2>
      </three>
    </out>
  </xsl:template>
</xsl:stylesheet>

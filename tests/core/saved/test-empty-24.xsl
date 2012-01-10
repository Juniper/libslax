<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:xutil="http://xml.libslax.org/xutil" version="1.0" extension-element-prefixes="xutil">
  <xsl:template match="/">
    <out>
      <xsl:variable name="test" select="&quot;&lt;test&gt;&lt;one&gt;1&lt;/one&gt;&lt;/test&gt;&quot;"/>
      <xsl:variable name="res" select="xutil:string-to-xml($test)"/>
      <xsl:variable name="r2" select="xutil:string-to-xml(&quot;&lt;a&gt;&quot;, &quot;&lt;b&gt;&quot;, &quot;&lt;c&gt;&quot;, $test, &quot;&lt;/c&gt;&quot;, &quot;&lt;/b&gt;&quot;, &quot;&lt;/a&gt;&quot;)"/>
      <one>
        <res>
          <xsl:copy-of select="$res"/>
        </res>
        <r2>
          <xsl:copy-of select="$r2"/>
        </r2>
      </one>
      <xsl:variable name="ores" select="xutil:xml-to-string($res)"/>
      <xsl:variable name="or2" select="xutil:xml-to-string($r2)"/>
      <two>
        <ores>
          <xsl:value-of select="$ores"/>
        </ores>
        <or2>
          <xsl:value-of select="$or2"/>
        </or2>
      </two>
      <xsl:variable name="xres" select="xutil:string-to-xml($ores)"/>
      <xsl:variable name="xr2" select="xutil:string-to-xml($or2)"/>
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

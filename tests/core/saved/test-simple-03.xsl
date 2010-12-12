<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:template match="doc">
    <top>
      <xsl:choose>
        <xsl:when test="this and that or the/other[one]">
          <!-- SLAX has a simple "if" statement -->
          <simple>if</simple>
        </xsl:when>
        <xsl:when test="yet[more = &quot;fun&quot;]">
          <!-- ... and it has "else if" -->
        </xsl:when>
        <xsl:otherwise>
          <!-- ... and "else" -->
        </xsl:otherwise>
      </xsl:choose>
      <xsl:choose>
        <xsl:when test="starts-with(name, &quot;fe-&quot;)">
          <xsl:if test="mtu &lt; 1500">
            <!-- Deal with fast ethernet interfaces with low MTUs -->
          </xsl:if>
        </xsl:when>
        <xsl:otherwise>
          <xsl:if test="mtu &gt; 8096">
            <!-- Deal with non-fe interfaces with high MTUs -->
            <mtu>
              <xsl:value-of select="mtu"/>
            </mtu>
          </xsl:if>
        </xsl:otherwise>
      </xsl:choose>
    </top>
  </xsl:template>
</xsl:stylesheet>
